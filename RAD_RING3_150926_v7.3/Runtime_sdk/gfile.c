/* ============================================================================
GERENCIADOR DE ARQUIVOS v0.0.9 - LBF-VESA (Apenas Renomear Corrigido)
============================================================================ */
#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

/* Protótipos antecipados */
void Carregar_Diretorio(const char* path);
static const char* get_filename_from_path(const char* path);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 560;
const int winHeight = 560;

TGUIControl* PathEdit    = NULL;
TGUIControl* GoButton    = NULL;
TGUIControl* BtnRefresh  = NULL;
TGUIControl* FileList    = NULL;
TGUIControl* ActionEdit  = NULL;
TGUIControl* BtnRename   = NULL;
TGUIControl* BtnDel      = NULL;
TGUIControl* BtnCopy     = NULL;
TGUIControl* BtnNewDir   = NULL;
TGUIControl* ExecEdit    = NULL;
TGUIControl* BtnExecutar = NULL;
TGUIControl* LabelStatus = NULL;
TGUIControl* DebugMemo   = NULL;
TGUIControl* FilterEdit  = NULL;

/* Helper para buscar substring sem diferenciar maiúsculas/minúsculas */
static bool contains_substring_ci(const char* str, const char* sub) {
    if (!sub || sub[0] == '\0') return true;
    if (!str) return false;
    
    int len_str = (int)strlen(str);
    int len_sub = (int)strlen(sub);
    if (len_sub > len_str) return false;

    for (int i = 0; i <= len_str - len_sub; i++) {
        bool match = true;
        for (int j = 0; j < len_sub; j++) {
            char c1 = str[i + j];
            char c2 = sub[j];
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

/* Helper para formatar tamanhos em B, KB ou MB */
static void format_bytes(char* out, uint32_t bytes) {
    if (bytes >= 1024 * 1024) {
        uint32_t mb = bytes / (1024 * 1024);
        char num[16];
        itoa(mb, num, 10);
        strcpy(out, num);
        strcat(out, " MB");
    } else if (bytes >= 1024) {
        uint32_t kb = bytes / 1024;
        char num[16];
        itoa(kb, num, 10);
        strcpy(out, num);
        strcat(out, " KB");
    } else {
        char num[16];
        itoa(bytes, num, 10);
        strcpy(out, num);
        strcat(out, " B");
    }
}

void Log_Debug(const char* msg) {
    if (DebugMemo) {
        GUI_Memo_AddStr(DebugMemo, msg);
        GUI_Memo_AddStr(DebugMemo, "\n");
    }
}

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return get_key();
}

void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

/* ---------------- helpers de tratamento de string e caminho ---------------- */
static char* trim_str(char* str) {
    if (!str) return "";
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

static const char* strip_drive(const char* path) {
    if (!path) return "";
    if (path[0] && path[1] == ':') path += 2;
    while (*path == '/') path++;
    return path;
}

static const char* get_filename_from_path(const char* path) {
    if (!path) return "";
    const char* last_slash = NULL;
    for (const char* p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            last_slash = p;
        }
    }
    if (last_slash) return last_slash + 1;
    return path;
}

static void path_join(char* out, const char* base, const char* name) {
    strcpy(out, base);
    int L = (int)strlen(out);
    if (L > 0 && out[L - 1] != '/') strcat(out, "/");
    strcat(out, name);
}

static void path_parent(char* out, const char* cur) {
    strcpy(out, cur);
    int n = (int)strlen(out);
    while (n > 0 && out[n - 1] == '/') n--;
    while (n > 0 && out[n - 1] != '/') n--;
    if (n <= 2) strcpy(out, "C:/");
    else out[n] = 0;
}

static void path_canon(char* out, const char* in) {
    if (!in || !in[0]) { strcpy(out, "C:/"); return; }
    strcpy(out, in);
    if (out[1] == ':') out[0] = 'C';
    else { char tmp[256]; strcpy(tmp, out); strcpy(out, "C:/"); strcat(out, tmp); }
    int L = (int)strlen(out);
    while (L > 3 && out[L - 1] == '/') { out[L - 1] = 0; L--; }
}

static void resolve_full_path(char* out, const char* cur_dir, const char* input_path) {
    if (!input_path || !input_path[0]) {
        strcpy(out, cur_dir);
        return;
    }
    if ((input_path[0] && input_path[1] == ':') || input_path[0] == '/') {
        path_canon(out, input_path);
        return;
    }
    char tmp[256];
    path_join(tmp, cur_dir, input_path);
    path_canon(out, tmp);
}

/* ---------------- carregar diretorio --------------------------------------- */
void Carregar_Diretorio(const char* path) {
    if (!FileList) return;
    GUI_ListView_Clear(FileList);

    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
    const char* p = path;
    if (p[0] && p[1] == ':') p += 2;
    while (*p == '/') p++;
    while (*p) {
        char comp[64]; int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = 0;
        while (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;
        if (sys_fat_chdir(comp) != 0) break;
    }

    if (strcmp(path, "C:/") != 0 && strcmp(path, "0:/") != 0 && strcmp(path, "/") != 0) {
        GUI_ListView_AddItem(FileList, "..", 0, 0x10);
    }

    /* Obtém o texto do filtro */
    char filter_buf[64] = {0};
    if (FilterEdit) {
        char* f_text = GUI_Edit_GetText(FilterEdit);
        if (f_text) {
            strncpy(filter_buf, f_text, 63);
            trim_str(filter_buf);
        }
    }

    char nome[64]; file_info_t md;
    int idx = 0, itens = 0;
    uint32_t tamanho_total_pasta = 0;

    while (sys_fat_readdir(idx, nome, &md) == 1) {
        if (strcmp(nome, ".") != 0 && strcmp(nome, "..") != 0) {
            /* Aplica o filtro de nome / extensão */
            if (filter_buf[0] == '\0' || contains_substring_ci(nome, filter_buf)) {
                GUI_ListView_AddItem(FileList, nome, md.size, md.attributes);
                itens++;
                if (!(md.attributes & 0x10)) {
                    tamanho_total_pasta += md.size;
                }
            }
        }
        idx++;
        if (idx > 500) break;
    }

    sys_fat_chdir("/");

    char str_tamanho[32];
    format_bytes(str_tamanho, tamanho_total_pasta);

    char st[256]; char n[12];
    itoa(itens, n, 10);
    strcpy(st, "itens: "); strcat(st, n);
    strcat(st, " | tamanho: "); strcat(st, str_tamanho);
    strcat(st, " | caminho: "); strcat(st, path);

    if (LabelStatus) GUI_Edit_SetText(LabelStatus, st);
}

/* ---------------- callbacks RAD -------------------------------------------- */
void OnBtnGoClick(void* sender) {
    char canon[256];
    path_canon(canon, GUI_Edit_GetText(PathEdit));
    GUI_Edit_SetText(PathEdit, canon);
    Carregar_Diretorio(canon);
}

void OnBtnRefreshClick(void* sender) {
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}

void OnBtnNewDirClick(void* sender) {
    char* action_text = GUI_Edit_GetText(ActionEdit);
    if (!action_text || action_text[0] == '\0') return;

    char temp_buffer[256];
    strncpy(temp_buffer, action_text, 255);
    temp_buffer[255] = '\0';
    char* clean_input = trim_str(temp_buffer);

    if (clean_input[0] == '\0') return;

    Log_Debug("--- NOVA PASTA ---");

    char cur_dir[256];
    strcpy(cur_dir, GUI_Edit_GetText(PathEdit));

    char full_path[256];
    resolve_full_path(full_path, cur_dir, clean_input);

    const char* clean_target = strip_drive(full_path);

    char msg_buf[256];
    strcpy(msg_buf, "CRIAR PASTA: "); strcat(msg_buf, clean_target); Log_Debug(msg_buf);

    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");

    int res = sys_fat_mkdir(clean_target);

    strcpy(msg_buf, "sys_fat_mkdir retorno: ");
    char num_str[12];
    itoa(res, num_str, 10);
    strcat(msg_buf, num_str);
    Log_Debug(msg_buf);

    Carregar_Diretorio(cur_dir);
    GUI_Edit_SetText(ActionEdit, "");
}

void OnBtnDelClick(void* sender) {
    char* action_text = GUI_Edit_GetText(ActionEdit);
    if (!action_text || action_text[0] == '\0') return;

    char temp_buffer[256];
    strncpy(temp_buffer, action_text, 255);
    temp_buffer[255] = '\0';
    char* clean_input = trim_str(temp_buffer);

    if (clean_input[0] == '\0') return;

    Log_Debug("--- DELETAR ARQUIVO ---");

    char cur_dir[256];
    strcpy(cur_dir, GUI_Edit_GetText(PathEdit));

    char full_path[256];
    resolve_full_path(full_path, cur_dir, clean_input);

    const char* clean_target = strip_drive(full_path);

    char msg_buf[256];
    strcpy(msg_buf, "DELETAR: "); strcat(msg_buf, clean_target); Log_Debug(msg_buf);

    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");

    int res = sys_fat_rm(clean_target);

    strcpy(msg_buf, "sys_fat_rm retorno: ");
    char num_str[12];
    itoa(res, num_str, 10);
    strcat(msg_buf, num_str);
    Log_Debug(msg_buf);

    Carregar_Diretorio(cur_dir);
    GUI_Edit_SetText(ActionEdit, "");
}

void Executar_Binario_Seguro(void) {
    char* caminho_elf = GUI_Edit_GetText(ExecEdit);
    if (!caminho_elf || caminho_elf[0] == '\0') return;

    char full_exec[256];
    resolve_full_path(full_exec, GUI_Edit_GetText(PathEdit), caminho_elf);

    const char* clean_exec = strip_drive(full_exec);

    Log_Debug("--- EXECUTAR BINARIO ---");
    char msg_buf[256];
    strcpy(msg_buf, "EXEC: "); strcat(msg_buf, clean_exec); Log_Debug(msg_buf);

    /* 1. Reseta o diretorio para a raiz */
    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");

    /* 2. Separa a pasta do nome do arquivo */
    char dir_part[256];
    strcpy(dir_part, clean_exec);
    char* last_slash = NULL;
    for (char* p = dir_part; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') last_slash = p;
    }

    const char* file_exec = get_filename_from_path(clean_exec);

    /* 3. Entra subpasta por subpasta se houver caminho */
    if (last_slash) {
        *last_slash = '\0';
        const char* p = dir_part;
        while (*p) {
            char comp[64]; int i = 0;
            while (*p && *p != '/' && *p != '\\' && i < 63) comp[i++] = *p++;
            comp[i] = 0;
            while (*p == '/' || *p == '\\') p++;
            if (!comp[0] || strcmp(comp, ".") == 0) continue;
            sys_fat_chdir(comp);
        }
    }

    /* 4. Tenta executar pelo nome do arquivo na pasta atual */
    sys_exec(file_exec);

    /* Fallback: tenta pelo caminho limpo completo */
    sys_exec(clean_exec);

    GUI_Edit_SetText(ExecEdit, "");
}

void OnBtnExecutarClick(void* sender) { Executar_Binario_Seguro(); }

void Tratar_Modificacao_Arquivo(bool is_rename) {
    char* action_text = GUI_Edit_GetText(ActionEdit);
    if (!action_text || action_text[0] == '\0') {
        Log_Debug("Erro: Texto da acao esta vazio!");
        return;
    }

    Log_Debug(is_rename ? "--- INICIO RENOMEAR ---" : "--- INICIO COPIA ---");

    char temp_buffer[256];
    strncpy(temp_buffer, action_text, 255);
    temp_buffer[255] = '\0';

    char* clean_input = trim_str(temp_buffer);

    if (strncmp(clean_input, "cp ", 3) == 0) clean_input += 3;
    else if (strncmp(clean_input, "mv ", 3) == 0) clean_input += 3;
    else if (strncmp(clean_input, "copy ", 5) == 0) clean_input += 5;
    else if (strncmp(clean_input, "ren ", 4) == 0) clean_input += 4;

    clean_input = trim_str(clean_input);

    char* space_ptr = NULL;
    int len = (int)strlen(clean_input);
    for (int i = 0; i < len; i++) {
        if (clean_input[i] == ' ') { space_ptr = &clean_input[i]; break; }
    }

    if (!space_ptr) {
        Log_Debug("Erro: Separe origem e destino com espaco!");
        return;
    }

    *space_ptr = '\0';
    char* arg1 = trim_str(clean_input);
    char* arg2 = trim_str(space_ptr + 1);

    if (*arg1 == '\0' || *arg2 == '\0') {
        Log_Debug("Erro: Origem ou destino invalidos!");
        return;
    }

    char cur_dir[256];
    strcpy(cur_dir, GUI_Edit_GetText(PathEdit));

    char full_arg1[256];
    char full_arg2[256];

    resolve_full_path(full_arg1, cur_dir, arg1);

    int arg2_len = (int)strlen(arg2);
    if (arg2[arg2_len - 1] == '/' || arg2[arg2_len - 1] == '\\') {
        char base_dest[256];
        resolve_full_path(base_dest, cur_dir, arg2);
        path_join(full_arg2, base_dest, get_filename_from_path(arg1));
    } else {
        resolve_full_path(full_arg2, cur_dir, arg2);
    }

    const char* clean_orig = strip_drive(full_arg1);
    const char* clean_dest = strip_drive(full_arg2);

    char msg_buf[256];
    strcpy(msg_buf, "ORIGEM: "); strcat(msg_buf, clean_orig); Log_Debug(msg_buf);
    strcpy(msg_buf, "DESTINO: "); strcat(msg_buf, clean_dest); Log_Debug(msg_buf);

    int res = 0;
    if (is_rename) {
        /* Apenas a operacao de renomear navega ate a pasta local */
        if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");

        char dir_part[256];
        strcpy(dir_part, clean_orig);
        char* last_slash = NULL;
        for (char* p = dir_part; *p != '\0'; p++) {
            if (*p == '/' || *p == '\\') last_slash = p;
        }

        const char* file_orig = get_filename_from_path(clean_orig);
        const char* file_dest = get_filename_from_path(clean_dest);

        if (last_slash) {
            *last_slash = '\0';
            const char* p = dir_part;
            while (*p) {
                char comp[64]; int i = 0;
                while (*p && *p != '/' && *p != '\\' && i < 63) comp[i++] = *p++;
                comp[i] = 0;
                while (*p == '/' || *p == '\\') p++;
                if (!comp[0] || strcmp(comp, ".") == 0) continue;
                sys_fat_chdir(comp);
            }
        }

        res = sys_fat_rename(file_orig, file_dest);

        if (res != 0) {
            if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
            res = sys_fat_rename(clean_orig, clean_dest);
        }

        strcpy(msg_buf, "sys_fat_rename retorno: ");
    } else {
        /* Logica exata original do gfile_5.c para copia */
        if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
        res = sys_fat_copy(clean_orig, clean_dest);
        strcpy(msg_buf, "sys_fat_copy retorno: ");
    }

    char num_str[12];
    itoa(res, num_str, 10);
    strcat(msg_buf, num_str);
    Log_Debug(msg_buf);

    Carregar_Diretorio(cur_dir);
    GUI_Edit_SetText(ActionEdit, "");
}

void OnBtnRenameClick(void* sender) { Tratar_Modificacao_Arquivo(true); }
void OnBtnCopyClick(void* sender)   { Tratar_Modificacao_Arquivo(false); }

void OnFileListChange(void* sender) {
    int idx = gui_get_prop(FileList, PROP_ITEM_INDEX);
    if (idx == -1) return;
    char nome_sel[64];
    uint32_t tam_arquivo = 0;
    uint8_t attributes = 0;
    GUI_ListView_GetItem(FileList, idx, nome_sel, &tam_arquivo, &attributes);

    if (attributes & 0x10) {
        char novo[256];
        if (strcmp(nome_sel, "..") == 0) path_parent(novo, GUI_Edit_GetText(PathEdit));
        else                             path_join(novo, GUI_Edit_GetText(PathEdit), nome_sel);
        GUI_Edit_SetText(PathEdit, novo);
        Carregar_Diretorio(novo);
    } else {
        GUI_Edit_SetText(ActionEdit, nome_sel);
    }
}

/* ---------------- main ----------------------------------------------------- */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Gerenciador de Arquivos", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Gerenciador de Arquivos v0.0.9", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* Linha do caminho e filtro */
    GUI_CreateLabel(&MyApp, 10, 42, "Caminho:");
    PathEdit   = GUI_CreateEdit(&MyApp, 70, 38, 180, 25, "C:/", NULL);

    GUI_CreateLabel(&MyApp, 258, 42, "Filtro:");
    FilterEdit = GUI_CreateEdit(&MyApp, 308, 38, 80, 25, "", NULL);

    GoButton   = GUI_CreateButton(&MyApp, 396, 38, 76, 25, "Navegar", OnBtnGoClick);
    BtnRefresh = GUI_CreateButton(&MyApp, 478, 38, 72, 25, "Reler",   OnBtnRefreshClick);

    FileList = GUI_CreateListView(&MyApp, 10, 75, 540, 230, OnFileListChange);
    gui_set_prop(FileList, PROP_ITEM_INDEX, -1);

    /* Linha de acao */
    GUI_CreateLabel(&MyApp, 10, 317, "Acao:");
    ActionEdit = GUI_CreateEdit(&MyApp, 60, 313, 490, 25, "", NULL);
    BtnRename  = GUI_CreateButton(&MyApp, 10,  345, 125, 25, "Renomear",   OnBtnRenameClick);
    BtnDel     = GUI_CreateButton(&MyApp, 145, 345, 125, 25, "Deletar",    OnBtnDelClick);
    BtnCopy    = GUI_CreateButton(&MyApp, 280, 345, 125, 25, "Copiar",     OnBtnCopyClick);
    BtnNewDir  = GUI_CreateButton(&MyApp, 415, 345, 135, 25, "Nova Pasta", OnBtnNewDirClick);

    /* Linha de execucao */
    GUI_CreateLabel(&MyApp, 10, 380, "Executar:");
    ExecEdit    = GUI_CreateEdit(&MyApp, 85, 376, 360, 25, "", NULL);
    BtnExecutar = GUI_CreateButton(&MyApp, 452, 376, 98, 25, "Executar", OnBtnExecutarClick);

    /* Status e Memo de Debug */
    LabelStatus = GUI_CreateLabel(&MyApp, 10, 410, "itens: 0 | caminho: C:/");
    GUI_CreateLabel(&MyApp, 10, 430, "Debug Log:");
    
    DebugMemo = GUI_CreateMemo(&MyApp, 10, 448, 540, 100);
    gui_set_prop(DebugMemo, PROP_COLOR, 0x000000);
    Log_Debug("LOG DE DEBUG:");

    g_focused_control = (void*)PathEdit;
    ultimo_controle_focado = (void*)PathEdit;
    gui_set_prop(PathEdit, PROP_SET_FOCUS, 1);

    Carregar_Diretorio("C:/");
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
            precisa_redesenhar = true;
        }
        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
        else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }
        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                GUI_ProcessKeyboard(&MyApp, key);
                precisa_redesenhar = true;
            }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                    if      (GoButton    && rel_x >= GoButton->Left    && rel_x < GoButton->Left    + GoButton->Width    && rel_y >= GoButton->Top    && rel_y < GoButton->Top    + GoButton->Height)    gui_set_prop(GoButton,    PROP_STATE, 2);
                    else if (BtnRefresh  && rel_x >= BtnRefresh->Left  && rel_x < BtnRefresh->Left  + BtnRefresh->Width  && rel_y >= BtnRefresh->Top  && rel_y < BtnRefresh->Top  + BtnRefresh->Height)  gui_set_prop(BtnRefresh,  PROP_STATE, 2);
                    else if (BtnRename   && rel_x >= BtnRename->Left   && rel_x < BtnRename->Left   + BtnRename->Width   && rel_y >= BtnRename->Top   && rel_y < BtnRename->Top   + BtnRename->Height)   gui_set_prop(BtnRename,   PROP_STATE, 2);
                    else if (BtnDel      && rel_x >= BtnDel->Left      && rel_x < BtnDel->Left      + BtnDel->Width      && rel_y >= BtnDel->Top      && rel_y < BtnDel->Top      + BtnDel->Height)      gui_set_prop(BtnDel,      PROP_STATE, 2);
                    else if (BtnCopy     && rel_x >= BtnCopy->Left     && rel_x < BtnCopy->Left     + BtnCopy->Width     && rel_y >= BtnCopy->Top     && rel_y < BtnCopy->Top     + BtnCopy->Height)     gui_set_prop(BtnCopy,     PROP_STATE, 2);
                    else if (BtnNewDir   && rel_x >= BtnNewDir->Left   && rel_x < BtnNewDir->Left   + BtnNewDir->Width   && rel_y >= BtnNewDir->Top   && rel_y < BtnNewDir->Top   + BtnNewDir->Height)   gui_set_prop(BtnNewDir,   PROP_STATE, 2);
                    else if (BtnExecutar && rel_x >= BtnExecutar->Left && rel_x < BtnExecutar->Left + BtnExecutar->Width && rel_y >= BtnExecutar->Top && rel_y < BtnExecutar->Top + BtnExecutar->Height) gui_set_prop(BtnExecutar, PROP_STATE, 2);
                    events_process_mouse(rel_x, rel_y, 1, 0);
                    if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                        precisa_redesenhar = true;
                        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                    }
                }
                IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
            }
            if (mouse_hold_timer > 0) {
                mouse_hold_timer--;
                if (mouse_hold_timer == 0) {
                    if (GoButton)    gui_set_prop(GoButton,    PROP_STATE, 0);
                    if (BtnRefresh)  gui_set_prop(BtnRefresh,  PROP_STATE, 0);
                    if (BtnRename)   gui_set_prop(BtnRename,   PROP_STATE, 0);
                    if (BtnDel)      gui_set_prop(BtnDel,      PROP_STATE, 0);
                    if (BtnCopy)     gui_set_prop(BtnCopy,     PROP_STATE, 0);
                    if (BtnNewDir)   gui_set_prop(BtnNewDir,   PROP_STATE, 0);
                    if (BtnExecutar) gui_set_prop(BtnExecutar, PROP_STATE, 0);
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
