
#include "emu_labels.h"
#include "emulator.h"
#include "emu_tty.h"
#include "gui_console.h"
#include "utils.h"
#include "machine.h"

#include <string>
#include <cstring>
#include <list>
#include <stdio.h>

std::list<std::string> labels[65536];

const char *kernel_label_file = "n8_kernel.sym";
const char *monitor_label_file = "n8_monitor.sym";
void emu_labels_add(uint16_t addr, char * label) {
    labels[addr].remove(label);
    labels[addr].emplace_back(label);
}

std::list<std::string> emu_labels_get(uint16_t addr) {
    return labels[addr];
}
void emu_labels_clear() {
    for(int i = 0; i < 65536; i++) {
        while(labels[i].size() > 0) {
            labels[i].pop_back();
        }
    }
}
void emu_labels_console_list() {
    char log_msg[256] {0};

    for(int i = 0; i < 65536; i++) {
        for(auto label : labels[i]) {
            if(label.size() == 0) continue;
            snprintf(log_msg, 256, "addr: %4.4x   == %s\r\n", i, label.c_str());
            gui_con_printmsg(log_msg);

        }
    }
}
static void load_label_file(const char *path) {
    printf("Loading Symbols from '%s'\r\n", path);fflush(stdout);
    FILE *fp = fopen(path, "r");
    if(!fp) {
        printf("WARNING: Cannot open symbol file '%s' (skipped)\r\n", path);
        fflush(stdout);
        return;
    }

    int len;
    while(1) {
        char *line = NULL;
        size_t line_len = 256;
        char cmd[8] {0};
        char args[256] {0};
        char addr[8] {0};
        char label[192] {0};

        if((len = getline(&line,&line_len,fp)) == -1) {
            break;
        }
        if(*(line+len-1) == '\n') *(line+len-1) = 0;
        if(*(line+len-2) == '\r') *(line+len-2) = 0;

        if(sscanf(line,"%8s %196c", cmd, args) <= 0) {
            continue;
        };
        if(strncmp(cmd, "al", 2) == 0) {
            if(sscanf(args,"%s .%s", addr, label) <= 0) {
                continue;
            };
            emu_labels_add(htoi(addr), label);
        }
        else {
            printf("unknown cmd: %s\r\n", cmd); fflush(stdout);
        }

    }
    fclose(fp);
}

void emu_labels_load() {
    emu_labels_clear();
    load_label_file(kernel_label_file);
    load_label_file(monitor_label_file);
}

void emu_labels_init() {
    emu_labels_load();
}