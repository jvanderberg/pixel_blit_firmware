# Lib: FSEQ Parser
add_library(fseq_parser 
    src/fseq_parser.c
)

target_include_directories(fseq_parser PUBLIC include)

# Allow tests to find headers
target_include_directories(fseq_parser INTERFACE include)
