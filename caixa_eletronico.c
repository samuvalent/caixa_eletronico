
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARQ_CONTAS     "contas.dat"
#define ARQ_TEMP       "contas.tmp"
#define ARQ_TRANSACOES "transacoes.dat"

typedef enum { CONTA_ENCERRADA, CONTA_ATIVA } SituacaoConta;

typedef struct {
    long numero;
    char titular[100];
    char cpf[15];
    double saldo;
    SituacaoConta situacao;
} Conta;

typedef enum {
    DEPOSITO, SAQUE, TRANSFERENCIA_ENVIADA, TRANSFERENCIA_RECEBIDA
} TipoTransacao;

typedef struct {
    long conta;
    long contaRelacionada;
    TipoTransacao tipo;
    double valor;
} Transacao;

typedef enum {
    OP_OK, OP_ERRO_ARQUIVO, OP_NAO_EXISTE, OP_INATIVA,
    OP_VALOR_INVALIDO, OP_SALDO_INSUFICIENTE, OP_MESMA_CONTA
} Resultado;

/* Entrada de dados */

static void lerTexto(const char *msg, char *dest, size_t tam)
{
    printf("%s", msg);
    if (fgets(dest, (int)tam, stdin) == NULL) { dest[0] = '\0'; return; }
    dest[strcspn(dest, "\n")] = '\0';
}

static long lerLong(const char *msg)
{
    char buf[64], *fim;
    long v;
    for (;;) {
        lerTexto(msg, buf, sizeof buf);
        v = strtol(buf, &fim, 10);
        if (fim != buf && *fim == '\0') return v;
        printf("Entrada invalida. Digite um numero inteiro.\n");
    }
}

static double lerDouble(const char *msg)
{
    char buf[64], *fim;
    double v;
    for (;;) {
        lerTexto(msg, buf, sizeof buf);
        for (char *p = buf; *p; p++) if (*p == ',') *p = '.';
        v = strtod(buf, &fim);
        if (fim != buf && *fim == '\0') return v;
        printf("Entrada invalida. Digite um valor numerico.\n");
    }
}


static FILE *abrirContas(void)
{
    FILE *f = fopen(ARQ_CONTAS, "rb+");
    if (f == NULL) f = fopen(ARQ_CONTAS, "wb+");   /* cria se nao existe */
    if (f == NULL) perror("Erro ao abrir " ARQ_CONTAS);
    return f;
}


{
    rewind(f);
    for (;;) {
        long atual = ftell(f);
        if (fread(c, sizeof(Conta), 1, f) != 1) return 0;
        if (c->numero == numero) {
            if (pos) *pos = atual;
            return 1;
        }
    }
}

static int gravarConta(FILE *f, long pos, const Conta *c)
{
    if (fseek(f, pos, SEEK_SET) != 0) return 0;
    if (fwrite(c, sizeof(Conta), 1, f) != 1) return 0;
    fflush(f);
    return 1;
}

static void registrarTransacao(long conta, long relacionada,
                               TipoTransacao tipo, double valor)
{
    FILE *f = fopen(ARQ_TRANSACOES, "ab");
    if (f == NULL) { perror("Erro ao abrir " ARQ_TRANSACOES); return; }
    Transacao t = { conta, relacionada, tipo, valor };
    fwrite(&t, sizeof t, 1, f);
    fclose(f);
}

static const char *textoSituacao(SituacaoConta s)
{
    return s == CONTA_ATIVA ? "ATIVA" : "ENCERRADA";
}

static void mostrarResultado(Resultado r)
{
    switch (r) {
    case OP_OK:                 printf("Operacao realizada com sucesso.\n"); break;
    case OP_ERRO_ARQUIVO:       printf("Erro ao acessar o arquivo.\n"); break;
    case OP_NAO_EXISTE:         printf("Conta nao encontrada.\n"); break;
    case OP_INATIVA:            printf("Conta encerrada: operacao negada.\n"); break;
    case OP_VALOR_INVALIDO:     printf("Valor invalido (deve ser maior que zero).\n"); break;
    case OP_SALDO_INSUFICIENTE: printf("Saldo insuficiente.\n"); break;
    case OP_MESMA_CONTA:        printf("Origem e destino devem ser diferentes.\n"); break;
    }
}


static Resultado depositarCore(FILE *f, long numero, double valor)
{
    Conta c; long pos;
    if (!buscarConta(f, numero, &c, &pos)) return OP_NAO_EXISTE;
    if (c.situacao != CONTA_ATIVA)         return OP_INATIVA;
    if (valor <= 0)                        return OP_VALOR_INVALIDO;
    c.saldo += valor;
    return gravarConta(f, pos, &c) ? OP_OK : OP_ERRO_ARQUIVO;
}

static Resultado sacarCore(FILE *f, long numero, double valor)
{
    Conta c; long pos;
    if (!buscarConta(f, numero, &c, &pos)) return OP_NAO_EXISTE;
    if (c.situacao != CONTA_ATIVA)         return OP_INATIVA;
    if (valor <= 0)                        return OP_VALOR_INVALIDO;
    if (c.saldo < valor)                   return OP_SALDO_INSUFICIENTE;
    c.saldo -= valor;
    return gravarConta(f, pos, &c) ? OP_OK : OP_ERRO_ARQUIVO;
}



static Resultado depositar(FILE *f, long numero, double valor)
{
    Resultado r = depositarCore(f, numero, valor);
    if (r == OP_OK) registrarTransacao(numero, 0, DEPOSITO, valor);
    return r;
}

static Resultado sacar(FILE *f, long numero, double valor)
{
    Resultado r = sacarCore(f, numero, valor);
    if (r == OP_OK) registrarTransacao(numero, 0, SAQUE, valor);
    return r;
}


static Resultado transferir(FILE *f, long origem, long destino, double valor)
{
    Conta co, cd;
    if (origem == destino)                       return OP_MESMA_CONTA;
    if (!buscarConta(f, origem, &co, NULL))      return OP_NAO_EXISTE;
    if (!buscarConta(f, destino, &cd, NULL))     return OP_NAO_EXISTE;
    if (co.situacao != CONTA_ATIVA || cd.situacao != CONTA_ATIVA)
                                                 return OP_INATIVA;
    if (valor <= 0)                              return OP_VALOR_INVALIDO;
    if (co.saldo < valor)                        return OP_SALDO_INSUFICIENTE;

    Resultado r = sacarCore(f, origem, valor);
    if (r != OP_OK) return r;
    r = depositarCore(f, destino, valor);
    if (r != OP_OK) {                            /* desfaz o saque */
        depositarCore(f, origem, valor);
        return r;
    }
    registrarTransacao(origem, destino, TRANSFERENCIA_ENVIADA, valor);
    registrarTransacao(destino, origem, TRANSFERENCIA_RECEBIDA, valor);
    return OP_OK;
}

static Resultado encerrar(FILE *f, long numero)
{
    Conta c; long pos;
    if (!buscarConta(f, numero, &c, &pos)) return OP_NAO_EXISTE;
    if (c.situacao != CONTA_ATIVA)         return OP_INATIVA;
    if (c.saldo != 0.0) {
        printf("So e possivel encerrar contas com saldo zero (saldo: R$ %.2f).\n", c.saldo);
        return OP_VALOR_INVALIDO;
    }
    c.situacao = CONTA_ENCERRADA;
    return gravarConta(f, pos, &c) ? OP_OK : OP_ERRO_ARQUIVO;
}

static Resultado reativar(FILE *f, long numero)
{
    Conta c; long pos;
    if (!buscarConta(f, numero, &c, &pos)) return OP_NAO_EXISTE;
    if (c.situacao == CONTA_ATIVA) {
        printf("A conta ja esta ativa.\n");
        return OP_VALOR_INVALIDO;
    }
    c.situacao = CONTA_ATIVA;
    return gravarConta(f, pos, &c) ? OP_OK : OP_ERRO_ARQUIVO;
}


static void cadastrarConta(FILE *f)
{
    Conta c, existente;
    memset(&c, 0, sizeof c);

    lerTexto("Nome do titular: ", c.titular, sizeof c.titular);
    lerTexto("CPF: ", c.cpf, sizeof c.cpf);
    c.numero = lerLong("Numero da conta: ");

    if (c.titular[0] == '\0' || c.cpf[0] == '\0' || c.numero <= 0) {
        printf("Dados invalidos. Cadastro cancelado.\n");
        return;
    }
    if (buscarConta(f, c.numero, &existente, NULL)) {
        printf("Ja existe uma conta com esse numero.\n");
        return;
    }
    c.saldo = 0.0;
    c.situacao = CONTA_ATIVA;

    fseek(f, 0, SEEK_END);
    if (fwrite(&c, sizeof c, 1, f) != 1) { printf("Erro ao gravar conta.\n"); return; }
    fflush(f);
    printf("Conta %ld cadastrada. Saldo: R$ 0,00 | Situacao: CONTA_ATIVA\n", c.numero);
}

static void consultarConta(FILE *f)
{
    Conta c;
    long n = lerLong("Numero da conta: ");
    if (!buscarConta(f, n, &c, NULL)) { printf("Conta nao encontrada.\n"); return; }
    printf("\nConta:    %ld\nTitular:  %s\nCPF:      %s\nSaldo:    R$ %.2f\nSituacao: %s\n",
           c.numero, c.titular, c.cpf, c.saldo, textoSituacao(c.situacao));
}

static void listarContas(FILE *f)
{
    int filtro = (int)lerLong("1-Todas  2-Somente ativas  3-Somente encerradas: ");
    Conta c;
    int total = 0;

    rewind(f);
    printf("\n%-8s %-30s %-15s %12s  %s\n", "Conta", "Titular", "CPF", "Saldo", "Situacao");
    while (fread(&c, sizeof c, 1, f) == 1) {
        if ((filtro == 2 && c.situacao != CONTA_ATIVA) ||
            (filtro == 3 && c.situacao != CONTA_ENCERRADA)) continue;
        printf("%-8ld %-30.30s %-15s %12.2f  %s\n",
               c.numero, c.titular, c.cpf, c.saldo, textoSituacao(c.situacao));
        total++;
    }
    if (total == 0) printf("Nenhuma conta para exibir.\n");
}

static void extrato(void)
{
    long n = lerLong("Numero da conta: ");
    FILE *f = fopen(ARQ_TRANSACOES, "rb");
    Transacao t;
    int total = 0;
    static const char *nomes[] = { "DEPOSITO", "SAQUE", "TRANSF. ENVIADA", "TRANSF. RECEBIDA" };

    if (f == NULL) { printf("Nenhuma transacao registrada.\n"); return; }
    printf("\nExtrato da conta %ld\n", n);
    while (fread(&t, sizeof t, 1, f) == 1) {
        if (t.conta != n) continue;
        printf("%-17s R$ %10.2f", nomes[t.tipo], t.valor);
        if (t.contaRelacionada) printf("  (conta %ld)", t.contaRelacionada);
        printf("\n");
        total++;
    }
    if (total == 0) printf("Sem movimentacoes.\n");
    fclose(f);
}

static void excluirFisicamente(FILE **f)
{
    FILE *tmp;
    Conta c;
    int removidas = 0;

    fclose(*f);
    *f = fopen(ARQ_CONTAS, "rb");
    tmp = fopen(ARQ_TEMP, "wb");
    if (*f == NULL || tmp == NULL) {
        perror("Erro ao abrir arquivos");
        if (*f) fclose(*f);
        if (tmp) fclose(tmp);
        *f = abrirContas();
        return;
    }
    while (fread(&c, sizeof c, 1, *f) == 1) {
        if (c.situacao == CONTA_ATIVA) fwrite(&c, sizeof c, 1, tmp);
        else removidas++;
    }
    fclose(*f);
    fclose(tmp);

    if (remove(ARQ_CONTAS) != 0 || rename(ARQ_TEMP, ARQ_CONTAS) != 0)
        perror("Erro ao substituir arquivo");
    else
        printf("%d conta(s) encerrada(s) removida(s) definitivamente.\n", removidas);

    *f = abrirContas();
}

static void menu(void)
{
    printf("\n===== CAIXA ELETRONICO =====\n"
           "1 - Cadastrar conta\n2 - Consultar conta\n3 - Realizar deposito\n"
           "4 - Realizar saque\n5 - Realizar transferencia\n6 - Listar contas\n"
           "7 - Encerrar conta\n8 - Extrato\n9 - Reativar conta\n"
           "10 - Excluir contas encerradas (fisico)\n0 - Sair\n");
}

int main(void)
{
    FILE *f = abrirContas();
    if (f == NULL) return EXIT_FAILURE;

    int op;
    do {
        menu();
        op = (int)lerLong("Opcao: ");
        switch (op) {
        case 1: cadastrarConta(f); break;
        case 2: consultarConta(f); break;
        case 3: {
            long n = lerLong("Numero da conta: ");
            double v = lerDouble("Valor do deposito: ");
            mostrarResultado(depositar(f, n, v));
            break;
        }
        case 4: {
            long n = lerLong("Numero da conta: ");
            double v = lerDouble("Valor do saque: ");
            mostrarResultado(sacar(f, n, v));
            break;
        }
        case 5: {
            long o = lerLong("Conta de origem: ");
            long d = lerLong("Conta de destino: ");
            double v = lerDouble("Valor da transferencia: ");
            mostrarResultado(transferir(f, o, d, v));
            break;
        }
        case 6: listarContas(f); break;
        case 7: mostrarResultado(encerrar(f, lerLong("Numero da conta: "))); break;
        case 8: extrato(); break;
        case 9: mostrarResultado(reativar(f, lerLong("Numero da conta: "))); break;
        case 10: excluirFisicamente(&f); if (f == NULL) return EXIT_FAILURE; break;
        case 0: printf("Encerrando...\n"); break;
        default: printf("Opcao invalida.\n");
        }
    } while (op != 0);

    fclose(f);
    return EXIT_SUCCESS;
}
