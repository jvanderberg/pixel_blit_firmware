//! USB CDC serial logging and bootloader reboot support.

use embassy_executor::Spawner;
use embassy_rp::Peri;
use embassy_rp::peripherals::USB;
use embassy_rp::usb::Driver;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, Timer};
use embassy_usb::class::cdc_acm::{CdcAcmClass, State as CdcState};
use embassy_usb::{Builder as UsbBuilder, Config as UsbConfig};
use heapless::String;

/// Channel for log messages (capacity 8, max 128 chars each)
static LOG_CHANNEL: Channel<CriticalSectionRawMutex, String<128>, 8> = Channel::new();

/// Log a message to USB serial (non-blocking, drops if buffer full).
pub fn log(msg: &str) {
    let mut s: String<128> = String::new();
    s.push_str(msg).ok();
    s.push_str("\r\n").ok();
    LOG_CHANNEL.try_send(s).ok();
}

/// Log a formatted message to USB serial.
#[macro_export]
macro_rules! usb_println {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let mut s: heapless::String<128> = heapless::String::new();
        core::write!(&mut s, $($arg)*).ok();
        $crate::usb::log(&s);
    }};
}

/// Initialize USB with CDC and spawn tasks.
pub fn init(spawner: &Spawner, usb: Peri<'static, USB>) {
    let driver = Driver::new(usb, crate::Irqs);
    let mut config = UsbConfig::new(0x2E8A, 0x000A);
    config.manufacturer = Some("PixelBlit");
    config.product = Some("Pixel Blit Controller");
    config.serial_number = Some("001");
    config.max_power = 100;
    config.max_packet_size_0 = 64;

    static CONFIG_DESCRIPTOR: static_cell::StaticCell<[u8; 256]> = static_cell::StaticCell::new();
    static BOS_DESCRIPTOR: static_cell::StaticCell<[u8; 256]> = static_cell::StaticCell::new();
    static CONTROL_BUF: static_cell::StaticCell<[u8; 64]> = static_cell::StaticCell::new();
    static CDC_STATE: static_cell::StaticCell<CdcState> = static_cell::StaticCell::new();

    let mut builder = UsbBuilder::new(
        driver,
        config,
        CONFIG_DESCRIPTOR.init([0; 256]),
        BOS_DESCRIPTOR.init([0; 256]),
        &mut [],
        CONTROL_BUF.init([0; 64]),
    );

    let cdc_class = CdcAcmClass::new(&mut builder, CDC_STATE.init(CdcState::new()), 64);
    let usb_device = builder.build();

    match usb_task(usb_device) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn usb_task: {:?}", e),
    }
    match cdc_task(cdc_class) {
        Ok(token) => spawner.spawn(token),
        Err(e) => defmt::error!("Failed to spawn cdc_task: {:?}", e),
    }
}

#[embassy_executor::task]
async fn usb_task(mut usb: embassy_usb::UsbDevice<'static, Driver<'static, USB>>) {
    usb.run().await;
}

#[embassy_executor::task]
async fn cdc_task(mut class: CdcAcmClass<'static, Driver<'static, USB>>) {
    let mut prev_dtr = false;

    loop {
        class.wait_connection().await;

        // Send welcome message
        let _ = class.write_packet(b"Pixel Blit connected\r\n").await;

        loop {
            let dtr = class.dtr();
            let baud = class.line_coding().data_rate();

            // Detect 1200 baud touch (Arduino bootloader convention)
            if (prev_dtr && !dtr && baud == 1200) || (dtr && baud == 1200) {
                let _ = class.write_packet(b"Rebooting to BOOTSEL...\r\n").await;
                Timer::after_millis(100).await;
                embassy_rp::rom_data::reset_to_usb_boot(0, 0);
            }
            prev_dtr = dtr;

            // Send any pending log messages
            while let Ok(msg) = LOG_CHANNEL.try_receive() {
                let _ = class.write_packet(msg.as_bytes()).await;
            }

            // Check for input commands
            let mut buf = [0u8; 64];
            match embassy_time::with_timeout(Duration::from_millis(100), class.read_packet(&mut buf)).await {
                Ok(Ok(n)) if n > 0 => {
                    if buf.starts_with(b"reboot") {
                        let _ = class.write_packet(b"Rebooting to BOOTSEL...\r\n").await;
                        Timer::after_millis(100).await;
                        embassy_rp::rom_data::reset_to_usb_boot(0, 0);
                    } else if buf.starts_with(b"help") {
                        let _ = class.write_packet(b"Commands: help, reboot\r\n").await;
                    }
                }
                Ok(Ok(_)) => {}
                Ok(Err(_)) => break, // Disconnected
                Err(_) => {} // Timeout, continue
            }
        }
    }
}
