void kernel_main() {
    // Volatile kulcsszó: megakadályozza, hogy a fordító optimalizálja a memóriaírást
    volatile unsigned char* video_memory = (volatile unsigned char*) 0xB8000;
    const char* str = "AnimOS Experience v1.0 64-bit Long Mode: Active!";
    
    // Képernyő törlése (80 oszlop * 25 sor * 2 bájt karakterenként)
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video_memory[i] = ' ';       // Karakter
        video_memory[i+1] = 0x07;    // Attribútum (szürke feketén)
    }

    // Szöveg kiírása a bal felső sarokba
    for (int i = 0; str[i] != '\0'; i++) {
        video_memory[i * 2] = (unsigned char)str[i];
        video_memory[i * 2 + 1] = 0x0B; // Világoskék szín
    }

    // Végtelen ciklus a CPU megállításával
    while(1) {
        __asm__ volatile("hlt");
    }
}