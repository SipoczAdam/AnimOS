void kernel_main() {
    char* video_memory = (char*) 0xB8000;
    const char* str = "OS Booted Successfully!";
    for (int i = 0; str[i] != '\0'; i++) {
        video_memory[i * 2] = str[i];
        video_memory[i * 2 + 1] = 0x0D;
    }
}