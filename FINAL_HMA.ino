String statusChWriteKey = "TG7IU43YL7SPMF41";  // Channel ID: 2128216

#include "DFRobot_EC.h"
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "DHTesp.h"
DHTesp dhtSensor;
#include <SoftwareSerial.h> //para comunicación esp01 y arduino  
SoftwareSerial esp8266(2, 7);  // Rx ==> Pin 2; TX ==> Pin7 

#define speed8266 9600 
#define HARDWARE_RESET A2//resetear esp01 
#define EC_PIN A1
#define PH_PIN A0
#define Offset 0.33  // después de calibrar sensor pH
#define ArrayLenth  40  
#define ONE_WIRE_BUS 4
const int DHT_PIN = 8;
int Nivel= 9;
int releRiego = 12;
int releUV = 13;
byte tanqueM = 3; // Motor tanque almacenamiento
byte aguaM = 6; // Motor agua
byte acidM = 11; // Motor sln ácida
byte baseM = 10; // Motor sln básica
byte nutM = 5; // Motor sln nutritiva concentrada

int pHArray[ArrayLenth];//promedios de ph
int pHArrayIndex = 0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float voltageCE, CE, TT = 25, voltagePH, PH, TC, HC;  

String datos;
DFRobot_EC ec;

// variables para establecer tiempos de activación relés
unsigned long tiempoRiegoInicio = 0;
unsigned long tiempoUVInicio = 0;
const unsigned long tiempoRiegoEncendido = 18000; // 18 segundos
const unsigned long tiempoRiegoApagado = 600000; // 10 minutos
const unsigned long tiempoUVEncendido = 1800000; // 30 minutos
const unsigned long tiempoUVApagado = 84600000; // 23.5 horas

int spare = 0;
boolean error;

void setup() {
  Serial.begin(9600);
  ec.begin();
  pinMode(tanqueM, OUTPUT);
  pinMode(aguaM, OUTPUT);
  pinMode(acidM, OUTPUT);
  pinMode(baseM, OUTPUT);
  pinMode(nutM, OUTPUT);
  pinMode(releRiego, OUTPUT);
  pinMode(releUV, OUTPUT); 
  pinMode(Nivel, INPUT_PULLUP);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  esp8266.begin(speed8266); 
  pinMode(HARDWARE_RESET, OUTPUT);
  digitalWrite(HARDWARE_RESET, HIGH);
  EspHardwareReset();


  // Inicializar el sensor de pH
  delay(1000);  // Esperar un segundo para la estabilización del sensor
  readpH();     // Realizar una lectura inicial para estabilizar el sensor de pH
 
    }

void loop() {
    //conexión wifi
    error = false;
  while (esp8266.available())  {
    Serial.write(esp8266.read());
  }
  while (Serial.available())  {
    esp8266.write(Serial.read());
  }
    //bobas sumergibles apagadas
    digitalWrite(aguaM, LOW);
    digitalWrite(baseM, LOW);
    digitalWrite(acidM, LOW);
    digitalWrite(nutM, LOW);
    //Bomba de tanque de almacenamiento
    millismotor(60000, 155,tanqueM);//Encendido 1 minuto
    millismotor(360000, 0,tanqueM);// se apaga por 7 minutos
    //nivel
    while (digitalRead(Nivel) == LOW) {
      Serial.println("BAJO");
      analogWrite(aguaM, 155);
    }
  static unsigned long timepoint = millis();
  if (millis() - timepoint > 20000U)  // 900000 // para mediciones cada 15 minutos
  {
    timepoint = millis();
    
    // Lectura de sensores
    TempAndHumidity data = dhtSensor.getTempAndHumidity();//Lee temperatura y humedad relativa de la cámara de crecimiento
    TC = data.temperature;
    HC = data.humidity;
    voltageCE = analogRead(EC_PIN) / 1024.0 * 5000;   // read the voltage
    TT = readTemperature();          // read your temperature sensor to execute temperature compensation
    CE = ec.readEC(voltageCE, TT);   // convert voltage to EC with temperature compensation
    
    // pH
    pHArray[pHArrayIndex++] = analogRead(PH_PIN);
    if (pHArrayIndex == ArrayLenth) pHArrayIndex = 0;
    voltagePH = avergearray(pHArray, ArrayLenth) * 5.0 / 1024;
    PH = 3.5 * voltagePH + Offset;
    
    // Datos leídos
    datos = String(TT, 1) + ";" + String(CE, 2) + ";" + String(PH, 2) + ";" + String(TC) +";" + String(HC) +  "\r\n";
    Serial.println(datos);
    writeThingSpeak();//escribir datos en thindSpeak
    if (error)  {
    Serial.println(" <<<< ERROR >>>>");
    delay(2000);  
    }
    //Condicionales
    if (PH> 6.5) {
        millismotor(2000, 155,acidM);
        } else {
        analogWrite(aguaM, 0);
    }
    if (PH < 5.5) {
        millismotor(3000, 155,baseM);
        } else {
        analogWrite(aguaM, 0);
    }
    if (CE > 2.5) {
        millismotor(3000, 155,aguaM);
        } else {
        analogWrite(aguaM, 0);
        }
    if (CE < 1.5) {
        millismotor(3000, 155,nutM);
        } else {
        analogWrite(aguaM, 0);
    }
    if (TC<14){
            Serial.println("Temperatura muy baja: " + String(TC) + "°C" );}
      else if(TC>35){
            Serial.println("Temperatura muy alta: " + String(TC) + "°C" );}
    if (HC<40){
             Serial.println("Humedad relativa muy baja: " + String(HC) + "%" );}
      else if(HC>85){
            Serial.println("Humedad relativa muy alta: " + String(HC) + "%" );} 
    }
    // Control de releRiego
  if (timepoint - tiempoRiegoInicio < tiempoRiegoEncendido) {
    digitalWrite(releRiego, HIGH);
  } else if (timepoint - tiempoRiegoInicio < tiempoRiegoEncendido + tiempoRiegoApagado) {
    digitalWrite(releRiego, LOW);
  } else {
    tiempoRiegoInicio = timepoint;
  }
// Control de releUV
  if (timepoint - tiempoUVInicio < tiempoUVEncendido) {
    digitalWrite(releUV, HIGH);
  } else if (timepoint - tiempoUVInicio < tiempoUVEncendido + tiempoUVApagado) {
    digitalWrite(releUV, LOW);
  } else {
    tiempoUVInicio = timepoint;
  }   
// Control bomba tanque solución nutritiva
 

  ec.calibration(voltageCE, TT);          // calibration process by Serial CMD// comentar lectura de temperatura
}

// leer temperatura del agua 
float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

// Función para controlar el motor utilizando millis()
void millismotor(const long interval, int velmotor, byte pinMotor) {
  unsigned long currentMillis = millis();
  unsigned long previousMillis = currentMillis;

  while (currentMillis - previousMillis <= interval) {
    analogWrite(pinMotor, velmotor);  
    currentMillis = millis();
  }

  analogWrite(pinMotor, 0);  
}



// Funciones sensor pH
void readpH() {
  for (int i = 0; i < ArrayLenth; ++i) {
    pHArray[i] = analogRead(PH_PIN);
    delay(50);  // Pequeño retraso entre las lecturas para la estabilización
  }
  voltagePH = avergearray(pHArray, ArrayLenth) * 5.0 / 1024;
  PH = 3.5 * voltagePH + Offset;
}

double avergearray(int* arr, int number) {
  int i;
  int max, min;
  double avg;
  long amount = 0;
  if (number <= 0) {
    Serial.println("Error number for the array to averaging!\n");
    return 0;
  }
  if (number < 5) {   // less than 5, calculated directly statistics
    for (i = 0; i < number; i++) {
      amount += arr[i];
    }
    avg = amount / number;
    return avg;
  } else {
    if (arr[0] < arr[1]) {
      min = arr[0]; max = arr[1];
    } else {
      min = arr[1]; max = arr[0];
    }
    for (i = 2; i < number; i++) {
      if (arr[i] < min) {
        amount += min;        // arr<min
        min = arr[i];
      } else {
        if (arr[i] > max) {
          amount += max;    // arr>max
          max = arr[i];
        } else {
          amount += arr[i]; // min<=arr<=max
        }
      }//if
    }//for
    avg = (double)amount / (number - 2);
  }//if
  return avg;
}
// función para resetear el módulo wifi y que se conecte normalmente
void EspHardwareReset(){
  Serial.println("Reseteando ESP8266...");
  digitalWrite(HARDWARE_RESET, LOW);
  delay(500); 
  digitalWrite(HARDWARE_RESET, HIGH);
  delay(5000); 
  if (!esp8266) {
    Serial.println("¡El reinicio del ESP8266 ha fallado!");
  }
  else{
    Serial.println("ESP8266 reiniciado correctamente.");
  }
}

void startThingSpeakCmd(){
  esp8266.flush();
    String cmd = "AT+CIPSTART=\"TCP\",\"";
  cmd += "184.106.153.149";
  cmd += "\",80";
  esp8266.println(cmd);
  Serial.print("enviado ==> Start cmd: ");
  Serial.println(cmd);

  if (esp8266.find("Error")){
    Serial.println("AT+CIPSTART error");
    error = true;
    return;
  }
}

String sendThingSpeakGetCmd(String getStr){
  String cmd = "AT+CIPSEND=";
  cmd += String(getStr.length());
  esp8266.println(cmd);
  Serial.print("enviado ==> lenght cmd: ");
  Serial.println(cmd);

  if (esp8266.find((char *)">")){
    esp8266.print(getStr);
    Serial.print("enviado ==> getStr: ");
    Serial.println(getStr);
    delay(500);
    
    String messageBody = "";
    while (esp8266.available()){
      String line = esp8266.readStringUntil('\n');
      if (line.length() == 1) 
      {
        messageBody = esp8266.readStringUntil('\n');
      }
    }
    Serial.print("MessageBody received: ");
    Serial.println(messageBody);
    return messageBody;
  }
  else {
    esp8266.println("AT+CIPCLOSE");
    Serial.println("ESP8266 CIPSEND ERROR: RESENDING");
    spare++;
    error = true;
    return "error";
  } 
}

void writeThingSpeak(){
  startThingSpeakCmd();
  String getStr = "GET /update?api_key=";
  getStr += statusChWriteKey;
  getStr +="&field1=";
  getStr += String(TC);//Temperatura cámara de crecimiento
  getStr +="&field3=";
  getStr += String(TT);//Temperatura sln nutritiva
  getStr +="&field4=";
  getStr += String(HC);//Humedad relativa cámara de crecimiento
  getStr +="&field5=";
  getStr += String(PH);//pH sln nutritiva
  getStr +="&field6=";
  getStr += String(CE);//ce sln nutritiva
  sendThingSpeakGetCmd(getStr); 
} 

