#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"

// -------------------- CONFIGURAÇÃO DO TOUCH --------------------
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// -------------------- CONFIGURAÇÃO DO DISPLAY --------------------
TFT_eSPI tft = TFT_eSPI();
#define SCREEN_W 320
#define SCREEN_H 240

// -------------------- VARIÁVEIS --------------------
String cpfDigitado = "";
String cpfConfirmado = ""; 
String categoriaSelecionada = "";

bool dadosEnviados = false;

enum Tela { TECLADO, CATEGORIA, CARREGANDO, MENSAGEM_FINAL, CPF_INVALIDO };
Tela telaAtual = TECLADO;
unsigned long tempoEstado = 0;  
int passoCarregamento = 0;  
bool primeiroDesenho = true;

// -------------------- CORES & TAMANHO --------------------
uint16_t COR_FUNDO_TELA   = tft.color565(29, 59, 28);
uint16_t COR_BOTAO_BG     = TFT_BLUE;
uint16_t COR_BOTAO_TEXT   = TFT_WHITE;
uint16_t COR_BOTAO_BORDA  = TFT_WHITE;
uint16_t COR_BOTAO_APAGAR_BG = TFT_RED;
uint16_t COR_BOTAO_OK_BG     = TFT_GREEN;
int TEXTO_SIZE = 3;

// -------------------- TOUCH CALIBRADO --------------------
#define TS_MINX 200
#define TS_MAXX 3900
#define TS_MINY 200
#define TS_MAXY 3900

// -------------------- BOTÕES --------------------
struct Button {
  int x, y, w, h;
  String label;
  uint16_t colorBg;
  uint16_t colorText;
  uint16_t colorBorder;
};
Button buttons[12];
TFT_eSprite teclado = TFT_eSprite(&tft);

// -------------------- CATEGORIAS --------------------
const int numCategorias = 4;
String categorias[] = {"Computador", "Celular", "Bateria", "Pilha", "Outro"};
Button catButtons[numCategorias];

// ==================== FUNÇÃO AUXILIAR MULTILINHA ====================
void drawMultilineString(String text, int x, int y, int maxWidth, int textSize, uint16_t color) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, COR_FUNDO_TELA);
  tft.setTextSize(textSize);

  int start = 0;
  while (start < text.length()) {
    int len = 0;
    while (start + len < text.length() && len * 6 * textSize < maxWidth) len++;
    String linha = text.substring(start, start+len);
    tft.drawString(linha, x, y);
    y += 8 * textSize;
    start += len;
  }
}

void enviarParaAPI(String cpf, String categoria, int massa, String idLixeira) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi nao conectado, tentando reconectar...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("WiFi conectado!");
  }

  HTTPClient http;
  String endpoint = String(serverBase) + "/eletronicos";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");

  // Monta JSON
  String json = "{";
  json += "\"cpf\":\"" +cpf + "\",";
  json += "\"categoria\":\""  + categoria + "\",";
  json += "\"massa\":" + String(massa) + ",";
  json += "\"lixeiraDescarte\":\"" + idLixeira + "\"";
  json += "}";

  Serial.println(json);

  int httpCode = http.POST(json);
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("POST OK: " + response);
  } else {
    Serial.println("Falha no POST");
  }
  http.end();
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  SPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI);
  ts.begin();
  ts.setRotation(1);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COR_FUNDO_TELA);

  criarBotoes();
  desenharTeclado();
  atualizarCampoTexto();
}

// ==================== LOOP ====================
void loop() {
  switch(telaAtual) {
    case TECLADO:
      processarTeclado();
      break;

    case CATEGORIA:
      processarCategoria();
      break;

    case CPF_INVALIDO:
      if (millis() - tempoEstado >= 2000) {
        cpfDigitado = "";
        telaAtual = TECLADO;
        desenharTeclado();
        atualizarCampoTexto();
      }
      break;

    case CARREGANDO:
      mostrarTelaCarregamento();
      delay(100);
      break;

    case MENSAGEM_FINAL:
      mostrarMensagemFinal();
      break;
  }
}

// ==================== FUNÇÕES DO TECLADO ====================
void processarTeclado() {
  if (!ts.touched()) return;

  TS_Point p = ts.getPoint();
  int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_W);
  int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_H);

  for (int i = 0; i < 12; i++) {
    if (x >= buttons[i].x && x <= buttons[i].x + buttons[i].w &&
        y >= buttons[i].y && y <= buttons[i].y + buttons[i].h) {

      if (buttons[i].label == "<-") {
        if (cpfDigitado.length() > 0) cpfDigitado.remove(cpfDigitado.length() - 1);
      } else if (buttons[i].label == "OK") {
        bool valido = validarCPF(cpfDigitado);
        mostrarMensagem(valido);
        if (valido) {
          cpfConfirmado = cpfDigitado;
          telaAtual = CATEGORIA;
          mostrarTelaCategoria();
        }
      } else {
        if (cpfDigitado.length() < 11) cpfDigitado += buttons[i].label;
      }
      atualizarCampoTexto();
      delay(150);
      break;
    }
  }
}

// ==================== CATEGORIAS ====================
void mostrarTelaCategoria() {
  tft.fillScreen(COR_FUNDO_TELA);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(TEXTO_SIZE);
  tft.drawString("Selecione a categoria:", 10, 10);

  int topMargin = 50;
  int btnH = 40;
  int btnW = SCREEN_W - 40;

  for (int i = 0; i < numCategorias; i++) {
    int y = topMargin + i*(btnH + 10);
    catButtons[i] = {20, y, btnW, btnH, categorias[i], TFT_BLUE, TFT_WHITE, TFT_WHITE};
    tft.fillRect(catButtons[i].x, catButtons[i].y, catButtons[i].w, catButtons[i].h, catButtons[i].colorBg);
    tft.drawRect(catButtons[i].x, catButtons[i].y, catButtons[i].w, catButtons[i].h, catButtons[i].colorBorder);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(catButtons[i].label, catButtons[i].x + btnW/2, catButtons[i].y + btnH/2);
  }
}

void processarCategoria() {
  if (!ts.touched()) return;
  TS_Point p = ts.getPoint();
  int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_W);
  int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_H);

  for (int i=0; i<numCategorias; i++) {
    if (x >= catButtons[i].x && x <= catButtons[i].x + catButtons[i].w &&
        y >= catButtons[i].y && y <= catButtons[i].y + catButtons[i].h) {
      categoriaSelecionada = catButtons[i].label;
      telaAtual = CARREGANDO;
      tempoEstado = millis();
      primeiroDesenho = true;
      break;
    }
  }
}

// ==================== Criação e desenho dos botões do teclado ====================
void criarBotoes() {
  int rows = 4;
  int cols = 3;
  int topMargin = 60;
  int btnW = SCREEN_W / cols;
  int btnH = (SCREEN_H - topMargin) / rows;

  int num = 1;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      buttons[num-1] = {col * btnW, topMargin + row * btnH, btnW, btnH, String(num),
                        COR_BOTAO_BG, COR_BOTAO_TEXT, COR_BOTAO_BORDA};
      num++;
    }
  }

  buttons[9]  = {0, topMargin + 3*btnH, btnW, btnH, "<-", COR_BOTAO_APAGAR_BG, COR_BOTAO_TEXT, COR_BOTAO_BORDA};
  buttons[10] = {btnW, topMargin + 3*btnH, btnW, btnH, "0", COR_BOTAO_BG, COR_BOTAO_TEXT, COR_BOTAO_BORDA};
  buttons[11] = {2*btnW, topMargin + 3*btnH, btnW, btnH, "OK", COR_BOTAO_OK_BG, COR_BOTAO_TEXT, COR_BOTAO_BORDA};
}

void desenharTeclado() {
  teclado.createSprite(SCREEN_W, SCREEN_H);
  teclado.fillSprite(COR_FUNDO_TELA);

  for (int i = 0; i < 12; i++) {
    teclado.fillRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, buttons[i].colorBg);
    teclado.drawRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, buttons[i].colorBorder);
    teclado.setTextColor(buttons[i].colorText, buttons[i].colorBg);
    teclado.setTextDatum(MC_DATUM);
    teclado.setTextSize(TEXTO_SIZE);
    teclado.drawString(buttons[i].label, buttons[i].x + buttons[i].w/2, buttons[i].y + buttons[i].h/2);
  }

  teclado.pushSprite(0,0);
}

void atualizarCampoTexto() {
  tft.fillRect(0, 0, SCREEN_W, 60, COR_FUNDO_TELA);
  tft.drawRect(10, 10, SCREEN_W-20, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, COR_FUNDO_TELA);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(TEXTO_SIZE);

  String formatado = "";
  for (int i = 0; i < cpfDigitado.length(); i++) {
    formatado += cpfDigitado[i];
    if (i == 2 || i == 5) formatado += '.';
    if (i == 8) formatado += '-';
  }

  tft.drawString(formatado, 15, 20);
}

// ==================== VALIDAÇÃO CPF ====================
bool validarCPF(String cpf) {
  if (cpf.length() != 11) return false;
  bool allEqual = true;
  for (int i = 1; i < 11; i++) if (cpf[i] != cpf[0]) allEqual = false;
  if (allEqual) return false;

  int sum = 0;
  for (int i = 0; i < 9; i++) sum += (cpf[i] - '0') * (10 - i);
  int d1 = (sum % 11 < 2) ? 0 : 11 - (sum % 11);

  sum = 0;
  for (int i = 0; i < 10; i++) sum += (cpf[i] - '0') * (11 - i);
  int d2 = (sum % 11 < 2) ? 0 : 11 - (sum % 11);

  return (d1 == (cpf[9]-'0') && d2 == (cpf[10]-'0'));
}

void mostrarMensagem(bool valido) {
  tft.fillRect(0, SCREEN_H-50, SCREEN_W, 50, COR_FUNDO_TELA);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(TEXTO_SIZE);

  if (valido) {
    tft.setTextColor(TFT_GREEN, COR_FUNDO_TELA);
    tft.drawString("CPF VALIDO!", SCREEN_W/2, SCREEN_H-25);
  } else {
    tft.setTextColor(TFT_RED, COR_FUNDO_TELA);
    tft.drawString("CPF INVALIDO!", SCREEN_W/2, SCREEN_H-25);
    telaAtual = CPF_INVALIDO;
    tempoEstado = millis();
  }
}

// ==================== CARREGAMENTO ====================
void mostrarTelaCarregamento() {
  if (primeiroDesenho) {
    tft.fillScreen(COR_FUNDO_TELA);
    passoCarregamento = 0;
    primeiroDesenho = false;
    dadosEnviados = false; // reseta controle
  }

  int cx = SCREEN_W/2;
  int cy = SCREEN_H/2;

  tft.fillRect(0, cy-10, SCREEN_W, 30, COR_FUNDO_TELA); // limpa área do texto

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, COR_FUNDO_TELA);

  if (millis() - tempoEstado < 3000) {
    String dots = "";
    for (int i=0; i<(passoCarregamento%4); i++) dots += ".";
    tft.drawString("Analisando" + dots, cx, cy);
    passoCarregamento++;
  } else if (millis() - tempoEstado < 6000) {
    String dots = "";
    for (int i=0; i<(passoCarregamento%4); i++) dots += ".";
    tft.drawString("Enviando ao servidor" + dots, cx, cy);
    passoCarregamento++;

    // Simula envio API uma vez
    if (!dadosEnviados) {
      // Aqui você só precisa passar os parâmetros desejados:
      enviarParaAPI(cpfConfirmado, categoriaSelecionada, 150, idLixeira);
      dadosEnviados = true;
    }
  } else {
    telaAtual = MENSAGEM_FINAL;
    tempoEstado = millis();
    primeiroDesenho = true;
    dadosEnviados = false; // prepara próximo envio
  }
}

// ==================== MENSAGEM FINAL ====================
void mostrarMensagemFinal() {
  if (primeiroDesenho) {
    tft.fillScreen(COR_FUNDO_TELA);
    drawMultilineString("Parabens! Seus e-coins ja estao guardados", 10, SCREEN_H/2 - 20, SCREEN_W - 20, 2, TFT_GREEN);
    tempoEstado = millis();
    primeiroDesenho = false;
  }

  if (millis() - tempoEstado >= 5000) {
    telaAtual = TECLADO;
    cpfDigitado = "";
    categoriaSelecionada = "";
    desenharTeclado();
    atualizarCampoTexto();
  }
}
