# LBF-OS64-BITS_v4.6.5_Kernel_3.1_Runtime_7.3
SISTEMA OPERACIONAL  x86-64 BITS

# LBF-VESA OS — Release 1.0 "Runtime_Banco & Proteção de Dados"

Sistema operacional próprio (Ring 0 + Ring 3) com GUI VESA, sistema de arquivos
FAT32 nativo, banco de dados relacional embutido e um ecossistema de ferramentas
de diagnóstico/recuperação. Esta release consolida o **Runtime_Banco**
(banco de dados nativo em 3 camadas) e o **ciclo completo de proteção de dados**:
sanitização de FAT no mount, escrita auto-verificada, recuperação byte a byte
e snapshot/restore com quarentena.

---

## 1. Arquitetura

```
RING 3 (aplicações / runtimes)
├─ Runtime_Banco   core_banco → sys_banco (banco_syscall) → libbanco.h
│                  Apps: LBF Studio Create v1.4 (DDL) | Agenda LBF v1.3 (CRUD)
├─ Runtime_Cmd     core_cmd → sys_cmd (cmd_syscall) → lib_cmd.h
│                  App: SM_CMD v0.1 (bancada de testes de FS)
├─ Proteção        LBF Banco Backup v2.4 | LBF RECOVER v3.1 | HexView v1.3
└─ Shell/UI        Terminal LBF v4.4 | Gerenciador v0.0.9 | Bloco de Notas v1.3
────────────────────────────────────────────────────────────────────────────
RING 0 (kernel)
├─ FAT32 nativo (SATA/AHCI) com sanitize de FAT no mount e root-cluster guard
├─ fat32_write_file (overwrite seguro + dir extensível + rollback)
├─ fat32_write_file_at_offset (escrita aleatória read-modify-write)
├─ xcopy v4.0 (cópia em fatias, auto-verificada, sem órfãos)
└─ Syscalls Int 0x80 | K_TRY/K_EXCEPT (fault isolation) | IPC multi-janela
```

## 2. Runtime_Banco — banco de dados relacional nativo

Três camadas, no mesmo padrão dos runtimes de FS (mudou o core → religa o
`banco.o` → nenhum `.c` de aplicação muda):

| Camada | Papel |
|---|---|
| `core_banco/` | store (.CAT/.TBL), schema/spec, create, insert, select, update, delete, drop |
| `sys_banco/`  | dispatcher `banco_syscall(num, a1..a7)` — ponto único de entrada |
| `libbanco.h`  | wrappers `static inline` (`sys_banco_create_db`, `_insert`, `_select`, ...) |

API principal: `CREATE_DB`, `CREATE_TABLE` (spec DDL: `campo:I4:PK:AI:NU,...`),
`INSERT`, `SELECT` (por campo/valor), `UPDATE`/`DELETE` (por PK),
`LIST_TABLES`, `LIST_FIELDS`, `COUNT`, `DROP_TABLE`.

## 3. Formato on-disk (especificação)

**Catálogo `<BANCO>.CAT`** — magic `LBFCAT1`:

| Off | Tam | Campo |
|---|---|---|
| 0 | 8 | magic `LBFCAT1\0` |
| 8 | 4 | nº de entradas |
| 12+ | — | entradas (nome da tabela → arquivo `.TBL`) |

**Tabela `<BASE4><NNNN>.TBL`** — magic `LBFTBL1` (ex.: `CADA0001.TBL`):

| Off | Tam | Campo |
|---|---|---|
| 0 | 8 | magic `LBFTBL1\0` |
| 8 | 4 | `tbl_id` |
| 12 | 4 | `num_campos` |
| 16 | 4 | `num_regs` |
| 20 | 4 | `next_pk` (auto-incremento) |
| 24 | 4 | `rec_size` = 2 + Σ(tam) |
| 32 | 32×N | campos: `nome[16]`, `tipo`@16 (1=I1,2=I2,3=I4,4=Cn), `tam`@17, `flags`@18 (1=PK,2=AI,4=NU) |
| 32+32N | `rec_size`×regs | registros: byte0=`viva` (1=vivo,2=apagado), byte1=pad, campos a partir de @2 (LE; texto space-pad) |

## 4. Proteção de dados — 4 camadas

| Camada | Componente | Garantia |
|---|---|---|
| 1 | Kernel FAT32 | sanitize de `FAT[0/1/raiz]` no mount; alocador nunca entrega clusters 0/1/raiz; escrita com rollback e `free_chain` (zero órfãos) |
| 2 | Cores (banco/cmd) | toda gravação pós-verifica tamanho; cópia em fatias auto-verificada |
| 3 | RECOVER v3.1 | edição em buffer + `SALVAR ARQ`; `FIX MAGIC`, `FIX HEADER` (reconstrói header pelas evidências), `DIAGNOSTICO` byte a byte |
| 4 | BANCO BACKUP v2.4 | `SNAPSHOT` verificado byte a byte + `MANIFEST.TXT`; `RESTAURAR` com **quarentena obrigatória** (nunca sobrescreve sem guardar o estado atual) |

## 5. Testes verificados nesta release

- [x] Create banco/tabela via Studio Create (`rc=0`, spec gravada)
- [x] Insert/Select/Update via Agenda (registro vivo exibido)
- [x] Snapshot `BAK0001` → `VERIFY 2/2 IDENTICO` → `COMPLETO E VERIFICADO`
- [x] Restore idempotente (`JA IDENTICO (nada a gravar)`) + quarentena `QUAR0001`
- [x] Corrupção proposital de magic → `DIAGNOSTICO` aponta o byte → `FIX MAGIC`/`FIX HEADER` → estrutura OK
- [x] Cópia raiz→subpasta e dentro da pasta (`cp`, gfile, xcopy)
- [x] `chk` (consistência de leitura 2× + fatias) sem divergências

## 6. Limitações conhecidas / roadmap

- USB/EHCI congelado nesta release (`D:`/`E:` e escrita USB pendentes)
- `cp`/`mv` de **diretórios** inteiros não suportado (arquivos sim)
- `core_rmdir` (remoção de pasta) pendente no Runtime_Cmd
- Auditoria de banco multi-tabela (`BANCO CHECK`) planejada p/ próxima release
- Snapshot limitado a arquivos ≤ 4 KB por comparação inteira (bancos atuais: OK)

## 7. Build

```bash
# Ring 0
gcc -O3 -msse3 -m64 -ffreestanding -fno-stack-protector -fno-pie -Isystem -I. -c fs/*.c ...
# Runtime_Banco → banco.o (ld -r core+sys); apps linkam banco.o + libbanco.h
# Runtime_Cmd  → cmd.o  (ld -r core+sys); apps linkam cmd.o  + lib_cmd.h
```

## 8. Changelog (destaques)

- FAT32: sanitize no mount + root-guard (fim da classe de bug "raiz evaporada")
- xcopy v4.0: cópia chunked auto-verificada com cleanup de destino parcial
- Runtime_Banco completo (7 cores + dispatcher + lib) + 2 apps gráficos
- Banco Backup v2.4: snapshot/verify/restore com quarentena e sondas R0/R1
- RECOVER v3.1: recuperação em buffer com FIX MAGIC/FIX HEADER
- Terminal v4.4: dispatch table, aspas, history, redirect, AUTOEXEC
