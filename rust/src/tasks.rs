//! Async tasks for the firmware.

use defmt::{info, warn, error, debug};
use embassy_rp::gpio::{Input, Level, Output};
use embassy_rp::peripherals::{PIO1, SPI0};
use embassy_rp::pio::Pio;
use embassy_rp::spi::{Config as SpiConfig, Spi};
use embassy_rp::Peri;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use reducto::ActionChannel;
use embassy_time::{Duration, Instant, Timer};
use heapless::String;

use pixel_blit::action::Action;
use pixel_blit::fseq_parser::{FseqLayout, FseqHeader};
use pixel_blit::led_driver::{self, FrameStringConfig, WriteToken};
use pixel_blit::led_driver::buffer::{FRAME_CHANNEL, FREE_CHANNEL, FSEQ_REQUEST_CHANNEL, FSEQ_TOKEN_CHANNEL, FSEQ_RETURN_CHANNEL};
use pixel_blit::sdcard::hw::SdReader;
use pixel_blit::sdcard::{set_file_list, get_filename, get_short_filename, MAX_FILENAME_LEN, MAX_SHORT_NAME_LEN};
use embedded_sdmmc::{Mode, VolumeIdx};

use crate::Irqs;

/// Channel for dispatching actions to the main loop.
pub static ACTION_CHANNEL: ActionChannel<Action, 4> = ActionChannel::new();

/// Channel to trigger SD card rescan.
pub static SD_RESCAN_CHANNEL: Channel<CriticalSectionRawMutex, u8, 1> = Channel::new();

/// Command for FSEQ playback
#[derive(Clone)]
pub enum FseqCommand {
    /// Start playing a file by index
    Play {
        file_index: u8,
        string_configs: [pixel_blit::state::StringConfig; 32],
    },
    /// Stop playback
    Stop,
}

/// Channel for FSEQ playback commands
pub static FSEQ_COMMAND_CHANNEL: Channel<CriticalSectionRawMutex, FseqCommand, 2> = Channel::new();

// ============================================================================
// Button Tasks
// ============================================================================

#[embassy_executor::task]
pub async fn button_next_task(mut btn: Input<'static>) {
    loop {
        btn.wait_for_falling_edge().await;
        info!("Next button pressed");
        ACTION_CHANNEL.send(Action::ButtonNext).await;
        Timer::after_millis(200).await;
    }
}

#[embassy_executor::task]
pub async fn button_select_task(mut btn: Input<'static>) {
    loop {
        btn.wait_for_falling_edge().await;
        info!("Select button pressed");
        ACTION_CHANNEL.send(Action::ButtonSelect).await;
        Timer::after_millis(200).await;
    }
}

// ============================================================================
// Timer Task
// ============================================================================

#[embassy_executor::task]
pub async fn tick_task() {
    loop {
        Timer::after_secs(1).await;
        ACTION_CHANNEL.send(Action::Tick1s).await;
        let fps = led_driver::get_measured_fps();
        ACTION_CHANNEL.send(Action::RainbowFrameComplete { fps }).await;
    }
}

// ============================================================================
// SD Card Task
// ============================================================================

/// SPI frequency for SD card (40 MHz)
const SD_SPI_SPEED_HZ: u32 = 40_000_000;

#[embassy_executor::task]
pub async fn sd_card_task(
    mut spi0: Peri<'static, SPI0>,
    mut clk: Peri<'static, embassy_rp::peripherals::PIN_38>,
    mut mosi: Peri<'static, embassy_rp::peripherals::PIN_39>,
    mut miso: Peri<'static, embassy_rp::peripherals::PIN_36>,
    mut cs_pin: Peri<'static, embassy_rp::peripherals::PIN_37>,
    mut dma0: Peri<'static, embassy_rp::peripherals::DMA_CH0>,
    mut dma1: Peri<'static, embassy_rp::peripherals::DMA_CH1>,
    initial_board_id: u8,
) {
    info!("SD: waiting 1s before init...");
    Timer::after_millis(1000).await;

    let mut board_id = initial_board_id;
    let mut config_loaded = false;

    loop {
        debug!("SD: init at 25MHz...");

        let mut spi_config = SpiConfig::default();
        spi_config.frequency = SD_SPI_SPEED_HZ;
        let spi = Spi::new(
            spi0.reborrow(),
            clk.reborrow(),
            mosi.reborrow(),
            miso.reborrow(),
            dma0.reborrow(),
            dma1.reborrow(),
            spi_config,
        );
        let cs = Output::new(cs_pin.reborrow(), Level::High);

        match SdReader::new(spi, cs) {
            Ok(mut reader) => {
                info!("SD: card detected at 25MHz");

                if !config_loaded {
                    info!("SD: loading config.csv for board {}", board_id);
                    match reader.load_config(board_id) {
                        Ok(config) => {
                            info!("SD: config loaded - {} strings, max {} px",
                                config.string_count, config.max_pixel_count);
                            ACTION_CHANNEL.send(Action::SdConfigLoaded {
                                strings: config.strings,
                                string_count: config.string_count,
                                max_pixel_count: config.max_pixel_count,
                            }).await;
                            config_loaded = true;
                        }
                        Err(e) => {
                            warn!("SD: config error: {}", e.message());
                            ACTION_CHANNEL.send(Action::SdConfigError {
                                message: "No config.csv - defaults",
                            }).await;
                            config_loaded = true;
                        }
                    }
                }

                info!("SD: scanning for .fseq files...");
                match reader.scan_fseq_files() {
                    Ok(files) => {
                        let count = files.count;
                        info!("SD: found {} files", count);
                        set_file_list(files);
                        ACTION_CHANNEL.send(Action::SdFilesScanned { count }).await;
                    }
                    Err(e) => {
                        error!("SD: scan error: {}", e.message());
                        ACTION_CHANNEL.send(Action::SdCardError {
                            message: e.message(),
                        }).await;
                    }
                }
                debug!("SD: scan complete");

                // Now wait for commands (rescan or FSEQ playback)
                loop {
                    use embassy_futures::select::{select, Either};

                    match select(
                        SD_RESCAN_CHANNEL.receive(),
                        FSEQ_COMMAND_CHANNEL.receive(),
                    ).await {
                        Either::First(new_board_id) => {
                            info!("SD: rescan requested for board {}", new_board_id);
                            board_id = new_board_id;
                            break; // Exit inner loop to rescan
                        }
                        Either::Second(cmd) => {
                            match cmd {
                                FseqCommand::Play { file_index, string_configs } => {
                                    info!("SD: FSEQ play for index {}", file_index);
                                    play_fseq(&mut reader, file_index, &string_configs).await;
                                }
                                FseqCommand::Stop => {
                                    debug!("SD: FSEQ stop (not playing)");
                                }
                            }
                        }
                    }
                }
            }
            Err(e) => {
                error!("SD: init FAILED: {}", e.message());
                ACTION_CHANNEL.send(Action::SdCardError {
                    message: e.message(),
                }).await;
                Timer::after_millis(500).await;

                // Wait for rescan request
                board_id = SD_RESCAN_CHANNEL.receive().await;
                info!("SD: rescan requested for board {}", board_id);
            }
        }
    }
}

/// Play an FSEQ file from the SD card.
/// Takes over buffer control from the runner, writes pixels directly.
///
/// IMPORTANT: This function keeps the file open for the entire playback session.
/// No open/close per frame - just sequential reads from the same file handle.
async fn play_fseq(
    reader: &mut SdReader<'_>,
    file_index: u8,
    string_configs: &[pixel_blit::state::StringConfig; 32],
) {
    // Get filenames - display name for logging, short name for FAT operations
    let display_name: String<MAX_FILENAME_LEN> = get_filename(file_index as usize);
    let short_name: String<MAX_SHORT_NAME_LEN> = get_short_filename(file_index as usize);
    if short_name.is_empty() {
        error!("FSEQ: invalid file index {}", file_index);
        return;
    }

    info!("FSEQ: opening {} ({})", display_name.as_str(), short_name.as_str());

    // Open volume, directory, and file - these stay open for entire playback!
    let volume = match reader.vm.open_volume(VolumeIdx(0)) {
        Ok(v) => v,
        Err(_) => {
            error!("FSEQ: failed to mount volume");
            return;
        }
    };
    let root_dir = match volume.open_root_dir() {
        Ok(d) => d,
        Err(_) => {
            error!("FSEQ: failed to open root dir");
            return;
        }
    };
    let file = match root_dir.open_file_in_dir(short_name.as_str(), Mode::ReadOnly) {
        Ok(f) => f,
        Err(_) => {
            error!("FSEQ: file not found: {}", short_name.as_str());
            return;
        }
    };

    // Read header (file is already at position 0)
    let mut header_buf = [0u8; 32];
    match file.read(&mut header_buf) {
        Ok(32) => {}
        Ok(n) => {
            error!("FSEQ: short header read: {} bytes", n);
            return;
        }
        Err(_) => {
            error!("FSEQ: failed to read header");
            return;
        }
    }

    // Build layout from string configs - must match xLights model order
    let mut layout = FseqLayout::default();
    let mut total_channels: u32 = 0;
    for (i, cfg) in string_configs.iter().enumerate() {
        layout.string_lengths[i] = cfg.pixel_count;
        total_channels += cfg.pixel_count as u32 * 3;
        if cfg.pixel_count > 0 {
            layout.num_strings = (i + 1) as u8;
        }
    }

    // Parse header directly (no need for full parser - data is raw RGB)
    let header = match FseqHeader::parse(&header_buf) {
        Some(h) => h,
        None => {
            error!("FSEQ: invalid header (not FSEQ v2)");
            return;
        }
    };

    let fps = header.fps();
    let frame_count = header.frame_count;
    let channel_data_offset = header.channel_data_offset as u32;
    let file_channels = header.channel_count;

    info!("FSEQ: {} frames @ {} fps", frame_count, fps);
    info!("FSEQ: file has {} ch, layout expects {} ch", file_channels, total_channels);

    if file_channels != total_channels {
        warn!("FSEQ: channel count mismatch! Check config.csv");
    }

    // Build frame string configs
    let mut frame_configs = [FrameStringConfig::default(); 32];
    for (i, cfg) in string_configs.iter().enumerate() {
        frame_configs[i] = FrameStringConfig {
            length: cfg.pixel_count,
            color_order: cfg.color_order,
        };
    }

    // Seek to channel data start
    if file.seek_from_start(channel_data_offset).is_err() {
        error!("FSEQ: failed to seek to data offset");
        return;
    }

    // Request token takeover from runner
    info!("FSEQ: requesting buffer control");
    FSEQ_REQUEST_CHANNEL.send(()).await;
    let mut token = FSEQ_TOKEN_CHANNEL.receive().await;
    info!("FSEQ: got buffer, starting playback @ {} fps", fps);

    // Frame pacing: each frame goes out one step time after the previous one
    let frame_interval = Duration::from_micros(1_000_000 / fps as u64);

    // FPS measurement - accumulate 100 frames
    let mut total_loop_time: u64 = 0;
    let mut min_loop_time: u64 = u64::MAX;
    let mut max_loop_time: u64 = 0;

    // Playback state
    let mut frames_played: u32 = 0;
    let frame_size = file_channels as usize; // Bytes per frame = channel count

    // Double-buffered frame reads - read next frame while DMA outputs current
    let mut frame_buf_a = [0u8; 12000];
    let mut frame_buf_b = [0u8; 12000];
    let mut use_buf_a = true;

    // Configure LED buffer (first buffer)
    {
        let buffer = token.get_mut();
        for (i, cfg) in frame_configs.iter().enumerate() {
            buffer.set_string_config(i as u8, *cfg);
        }
    }

    // Send first frame and get second buffer to configure it too
    // (double buffering means we need to configure BOTH buffers)
    {
        let buffer = token.get_mut();
        buffer.clear();
    }
    let read_token = token.into_read_token();
    FRAME_CHANNEL.send(read_token).await;
    let mut token = FREE_CHANNEL.receive().await;
    {
        let buffer = token.get_mut();
        for (i, cfg) in frame_configs.iter().enumerate() {
            buffer.set_string_config(i as u8, *cfg);
        }
    }

    // Timing for logging
    let mut read_time: Duration;
    let mut parse_time: Duration;
    let mut wait_time: Duration;

    // Read first frame, parse it, send it, THEN start the overlapped loop
    let mut current_bytes = match file.read(&mut frame_buf_a[..frame_size]) {
        Ok(n) if n > 0 => n,
        Ok(_) => {
            error!("FSEQ: empty file");
            FSEQ_RETURN_CHANNEL.send(token).await;
            return;
        }
        Err(_) => {
            error!("FSEQ: initial read error");
            FSEQ_RETURN_CHANNEL.send(token).await;
            return;
        }
    };

    // Parse and send first frame to start the DMA pipeline
    {
        let buffer = token.get_mut();
        let mut offset = 0;
        for string_idx in 0..layout.num_strings {
            let string_len = layout.string_lengths[string_idx as usize] as usize;
            let byte_count = string_len * 3;
            if offset + byte_count <= current_bytes {
                buffer.set_string_pixels(string_idx, &frame_buf_a[offset..offset + byte_count]);
                offset += byte_count;
            }
        }
    }
    frames_played += 1;

    // Send first frame - DMA starts
    let read_token = token.into_read_token();
    FRAME_CHANNEL.send(read_token).await;
    let mut last_frame_time = Instant::now();

    'playback: loop {
        let loop_start = Instant::now();

        // Check for stop command
        if let Ok(cmd) = FSEQ_COMMAND_CHANNEL.try_receive() {
            match cmd {
                FseqCommand::Stop => {
                    info!("FSEQ: stop command");
                    break 'playback;
                }
                FseqCommand::Play { file_index: new_index, .. } => {
                    info!("FSEQ: switching to file {}", new_index);
                    break 'playback;
                }
            }
        }

        // Check for loop completion before reading next
        if frames_played >= frame_count {
            info!("FSEQ: loop complete ({} frames)", frames_played);
            ACTION_CHANNEL.send(Action::FseqLoopComplete).await;

            // Reset for next loop - seek back to data start
            if file.seek_from_start(channel_data_offset).is_err() {
                error!("FSEQ: failed to seek for loop");
                break 'playback;
            }
            frames_played = 0;
        }

        // READ next frame - DMA for previous frame runs during this!
        let read_start = Instant::now();
        let read_buf = if use_buf_a {
            &mut frame_buf_a[..frame_size]
        } else {
            &mut frame_buf_b[..frame_size]
        };
        current_bytes = match file.read(read_buf) {
            Ok(n) if n > 0 => n,
            Ok(_) => {
                warn!("FSEQ: unexpected EOF");
                break 'playback;
            }
            Err(_) => {
                error!("FSEQ: read error");
                break 'playback;
            }
        };
        read_time = read_start.elapsed();

        // WAIT for token - DMA should be done or nearly done
        let wait_start = Instant::now();
        let mut token = FREE_CHANNEL.receive().await;
        wait_time = wait_start.elapsed();

        // PARSE into the token we just received
        let parse_start = Instant::now();
        {
            let buffer = token.get_mut();
            let current_data = if use_buf_a {
                &frame_buf_a[..current_bytes]
            } else {
                &frame_buf_b[..current_bytes]
            };
            let mut offset = 0;
            for string_idx in 0..layout.num_strings {
                let string_len = layout.string_lengths[string_idx as usize] as usize;
                let byte_count = string_len * 3;
                if offset + byte_count <= current_data.len() {
                    buffer.set_string_pixels(string_idx, &current_data[offset..offset + byte_count]);
                    offset += byte_count;
                }
            }
        }
        parse_time = parse_start.elapsed();

        frames_played += 1;

        // FPS limiting - wait until frame time
        let target_time = last_frame_time + frame_interval;
        let now = Instant::now();
        if now < target_time {
            Timer::after(target_time - now).await;
        }
        last_frame_time = target_time; // Drift correction

        // SEND frame to output - starts DMA for this frame
        let send_start = Instant::now();
        let read_token = token.into_read_token();
        FRAME_CHANNEL.send(read_token).await;
        let send_time = send_start.elapsed();

        // Swap buffers for next iteration
        use_buf_a = !use_buf_a;

        // Accumulate loop time at very end
        let loop_time = loop_start.elapsed().as_micros() as u64;
        total_loop_time += loop_time;
        if loop_time < min_loop_time { min_loop_time = loop_time; }
        if loop_time > max_loop_time { max_loop_time = loop_time; }

        // Spike logging disabled for testing
        // if loop_time > 20000 {
        //     info!("SPIKE: ...");
        // }

        // Every 100 frames, report FPS
        if frames_played % 100 == 0 {
            let avg_frame_us = total_loop_time / 100;
            let measured_fps = if avg_frame_us > 0 { 1_000_000 / avg_frame_us } else { 0 };
            ACTION_CHANNEL.try_send(Action::FseqFpsUpdate { fps: measured_fps as u16 }).ok();
            // Log every 1000 frames only
            if frames_played % 1000 == 0 {
                info!("FSEQ: avg={}us min={}us max={}us fps={}", avg_frame_us, min_loop_time, max_loop_time, measured_fps);
            }
            total_loop_time = 0;
            min_loop_time = u64::MAX;
            max_loop_time = 0;
        }
    }

    // Token was sent before breaking, need to receive it
    let mut token = FREE_CHANNEL.receive().await;

    // Clear final frame before returning control
    token.get_mut().clear();
    let read_token = token.into_read_token();
    FRAME_CHANNEL.send(read_token).await;
    let token = FREE_CHANNEL.receive().await;

    // Reset FPS display
    ACTION_CHANNEL.send(Action::FseqFpsUpdate { fps: 0 }).await;

    // Return token to runner
    info!("FSEQ: returning buffer control");
    FSEQ_RETURN_CHANNEL.send(token).await;
}

// ============================================================================
// LED Tasks
// ============================================================================

#[embassy_executor::task]
pub async fn led_output_task(
    pio1: Peri<'static, PIO1>,
    dma: Peri<'static, embassy_rp::peripherals::DMA_CH2>,
) {
    Timer::after_millis(100).await;
    info!("LED Output: initializing PIO1");

    let Pio { mut common, sm0, .. } = Pio::new(pio1, Irqs);

    debug!("LED Output: configuring 32 parallel outputs");
    let output = led_driver::LedOutput::new_parallel_32(&mut common, sm0);
    info!("LED Output: running");

    led_driver::run_output_loop(output, dma).await
}

#[embassy_executor::task]
pub async fn led_runner_task(token: WriteToken) {
    info!("LED Runner: starting");
    Timer::after_millis(100).await;
    led_driver::run(token).await
}
