 /**
 * Code from https://developer.mbed.org/users/GregCr/code/SX1276PingPong/
 * This is a weather station that receives data from iRocks and relays the packet to an Access Point supposedly on a Raspberry PI
 */

#include "mbed.h"
#include "main.h"
#include "sx1276-hal.h"
#include "debug.h"
#include "Crypto.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
unsigned char myKEY[0x20]= {2,1,3,'D',1,8,9,4,8,'D',3,8,2,'F',1,9,4,9,'A',5,7,'F',8,8,4,4,4,1,'D'}; //256bit key
AES myAES(AES_256, myKEY); // will use ECB mode

/* Set this flag to '1' to display debug messages on the console */
#define DEBUG_MESSAGE   0

/* Set this flag to '1' to use the LoRa modulation or to '0' to use FSK modulation */
#define USE_MODEM_LORA  1
#define USE_MODEM_FSK   !USE_MODEM_LORA

#define MY_ID                       88
#define RF_FREQUENCYRECEIVE                               868200000 // Hz
#define RF_FREQUENCYSEND                             869200000 // Hz

#define TX_OUTPUT_POWER                                 14        // 14 dBm

#if USE_MODEM_LORA == 1

#define LORA_BANDWIDTH                              0         // [0: 125 kHz,
//  1: 250 kHz,
//  2: 500 kHz,
//  3: Reserved]
#define LORA_SPREADING_FACTOR                       12         // [SF7..SF12]
#define LORA_CODINGRATE                             1         // [1: 4/5,
//  2: 4/6,
//  3: 4/7,
//  4: 4/8]
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                         5         // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_FHSS_ENABLED                           false
#define LORA_NB_SYMB_HOP                            4
#define LORA_IQ_INVERSION_ON                        false
#define LORA_CRC_ENABLED                            true

#elif USE_MODEM_FSK == 1

#define FSK_FDEV                                    25000     // Hz
#define FSK_DATARATE                                19200     // bps
#define FSK_BANDWIDTH                               50000     // Hz
#define FSK_AFC_BANDWIDTH                           83333     // Hz
#define FSK_PREAMBLE_LENGTH                         5         // Same for Tx and Rx
#define FSK_FIX_LENGTH_PAYLOAD_ON                   false
#define FSK_CRC_ENABLED                             true

#else
#error "Please define a modem in the compiler options."
#endif

#define TX_TIMEOUT_VALUE                                5000000   // in us
#define RX_TIMEOUT_VALUE                                10000000   // in us

#define BUFFER_SIZE                                     32        // Define the payload size here

#if( defined ( TARGET_KL25Z ) || defined ( TARGET_LPC11U6X ) )
DigitalOut led(LED2);
#else
DigitalOut led(LED1);
#endif

/*
 *  Global variables declarations
 */
typedef RadioState States_t;
volatile States_t State = LOWPOWER;

SX1276MB1xAS Radio( OnTxDone, OnTxTimeout, OnRxDone, OnRxTimeout, OnRxError, NULL, NULL );

uint16_t BufferSize = BUFFER_SIZE;
uint8_t Buffer[BUFFER_SIZE];

int16_t RssiValue = 0.0;
int8_t SnrValue = 0.0;

// For the serial commnication
Serial pc(USBTX, USBRX);

static bool sending_weather_values = false;

int main()
{
    
    debug( "\n\n\r Weather station relay  \n\n\r" );

    // verify the connection with the board
    while( Radio.Read( REG_VERSION ) == 0x00  ) {
        debug( "Radio could not be detected!\n\r", NULL );
        wait( 1 );
    }

    debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1276MB1LAS ) ) , "\n\r > Board Type: SX1276MB1LAS < \n\r" );
    debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1276MB1MAS ) ) , "\n\r > Board Type: SX1276MB1MAS < \n\r" );

    Radio.SetChannel( RF_FREQUENCYRECEIVE);
#if USE_MODEM_LORA == 1

    debug_if( LORA_FHSS_ENABLED, "\n\n\r             > LORA FHSS Mode < \n\n\r");
    debug_if( !LORA_FHSS_ENABLED, "\n\n\r             > LORA Mode < \n\n\r");

    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                       LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                       LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                       LORA_CRC_ENABLED, LORA_FHSS_ENABLED, LORA_NB_SYMB_HOP,
                       LORA_IQ_INVERSION_ON, 2000000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                       LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                       LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON, 0,
                       LORA_CRC_ENABLED, LORA_FHSS_ENABLED, LORA_NB_SYMB_HOP,
                       LORA_IQ_INVERSION_ON, true );

#elif USE_MODEM_FSK == 1

    debug("\n\n\r              > FSK Mode < \n\n\r");
    Radio.SetTxConfig( MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, 0,
                       FSK_DATARATE, 0,
                       FSK_PREAMBLE_LENGTH, FSK_FIX_LENGTH_PAYLOAD_ON,
                       FSK_CRC_ENABLED, 0, 0, 0, 2000000 );

    Radio.SetRxConfig( MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE,
                       0, FSK_AFC_BANDWIDTH, FSK_PREAMBLE_LENGTH,
                       0, FSK_FIX_LENGTH_PAYLOAD_ON, 0, FSK_CRC_ENABLED,
                       0, 0, false, true );

#else

#error "Please define a modem in the compiler options."

#endif

    debug_if( DEBUG_MESSAGE, "Starting reception loop\r\n" );

    led = 0;

    Radio.Rx( RX_TIMEOUT_VALUE );
    while( 1 ) {
        switch( State ) {
            /*
             * Receiving sensors updates and other information from iRocks
             */
            case RX:
                pc.printf("\n RX mode activated \n");
                if( BufferSize > 0 ) {
                    led=!led;
                    Radio.Rx( RX_TIMEOUT_VALUE );
                }
                State = LOWPOWER;
                break;

            case RX_TIMEOUT:
                Radio.Rx( RX_TIMEOUT_VALUE );
                State = LOWPOWER;
                break;

            case RX_ERROR:
                State = LOWPOWER;
                break;

            case LOWPOWER:
                break;

            case TX:
                pc.printf("\n TX Mode activated \n");
                if( BufferSize > 0 )
                    {
                        wait_ms( 10 );
                        Radio.Send( Buffer, BufferSize );
                    }
                    pc.printf("\n Packet send OK!! \n");

                Radio.SetChannel( RF_FREQUENCYRECEIVE );
                State = RX;
                    //Radio.Rx( RX_TIMEOUT_VALUE ); // DONT FUCK WITH THIS FUNCTION U DIPSHIT
    

                
            default:
                State = LOWPOWER;
                break;
        }
    }
}


void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    Radio.Sleep( );
    BufferSize = size;
    memcpy( Buffer, payload, BufferSize );
    RssiValue = rssi;
    SnrValue = snr;
    State = RX;
    debug_if( DEBUG_MESSAGE, "> OnRxDone\n\r" );
    
    // Getting ID of source node
    int idS = payload[0];
    //pc.printf("Payload : %d" , payload[0]);
    // Getting RSSI value from source to compare
    union{
        int16_t i;
        struct{
            uint8_t v0, v1;
        } bytes;
    } rssiToReceive;
    
    //Getting RSSI values to compare
    rssiToReceive.bytes.v0=payload[1];
    rssiToReceive.bytes.v1=payload[2];
    int16_t rssi_received = rssiToReceive.i;
    
    if(idS <100){
        //It's an F3, we will get the compass value in degrees
        union{
            uint16_t i;
            struct{
                uint8_t v0, v1;
            }bytes;   
        }compass_ToReceive;
        compass_ToReceive.bytes.v0 = payload[3];
        compass_ToReceive.bytes.v1 = payload[4];
        Buffer[5] = MY_ID;
        uint16_t compassDegrees = compass_ToReceive.i;
        pc.printf("\n 2#%d#%d#%d#%d \n", idS, compassDegrees, MY_ID, rssi );
    }
    else{
        //It's an F4, we will get accelerometer values x,y,z from the payload  
        // Getting Accelerometer Values x,y,z
        union{
            int16_t i;
            struct{
                uint8_t v0,v1;
            } bytes;
        }accel_x_ToReceive;
        accel_x_ToReceive.bytes.v0 = payload[3];
        accel_x_ToReceive.bytes.v1 = payload[4];
        int16_t accel_x = accel_x_ToReceive.i;
        union{
            int16_t i;
            struct{
                uint8_t v0,v1;
            } bytes;
        }accel_y_ToReceive;
        accel_y_ToReceive.bytes.v0 = payload[5];
        accel_y_ToReceive.bytes.v1 = payload[6];
        int16_t accel_y = accel_y_ToReceive.i;
        union{
            int16_t i;
            struct{
                uint8_t v0,v1;
            } bytes;
        }accel_z_ToReceive;
        accel_z_ToReceive.bytes.v0 = payload[7];
        accel_z_ToReceive.bytes.v1 = payload[8];
        Buffer[9] = MY_ID;
        int16_t accel_z = accel_z_ToReceive.i;
        // print result on the serial port
        pc.printf("\n 3#%d#%d#%d#%d#%d#%d \n", idS, accel_x, accel_y, accel_z, MY_ID, rssi);
    }
    Radio.SetChannel( RF_FREQUENCYSEND );
    pc.printf("Setting Radio to TX to relay packet");
    State = TX;
}

void OnRxTimeout( void )
{
    Radio.Sleep( );
    Buffer[ BufferSize ] = 0;
    //State = RX;
    debug_if( DEBUG_MESSAGE, "> OnRxTimeout\n\r" );
    pc.printf("\n TIMEOUT RX (sending my weather data)\n");
    sending_weather_values = true;
                    int windDirection = rand() % 360 + 1;
                    int humidity = rand() % 10 + 50;
                    long temperature = rand() % 4 + 22;
                    float windSpeed = static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/10));
                    float water = (static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/40))) + 60;
                    float battery = 100.0f;
                    float pressure = (static_cast <float> (rand()) / (static_cast <float> (RAND_MAX/50))) + 1000;
                    
                    // Convertion to uint8_t and packaging
                    
                    // ID
                    Buffer[0] = MY_ID;
                    pc.printf("%d" , Buffer[0]);
                    
                    // Wind direction
                    union{
                        uint16_t i;
                        struct{
                            uint8_t v0, v1;
                        } bytes;
                    } windToSend;
                    
                    uint16_t windDir16 = (uint16_t) windDirection;
                    
                    windToSend.i = windDir16;
                    
                    Buffer[1] = windToSend.bytes.v0;
                    Buffer[2] = windToSend.bytes.v1;
                    
                    // Humidity
                    Buffer[3] = (uint8_t) humidity;
                    pc.printf("Humidity : %d" , Buffer[3]);
                    // Temperature
                    int8_t tempConv = (int8_t) temperature;
                    uint8_t tempToSend = (uint8_t) tempConv;
                    Buffer[4] = tempToSend;
                    
                    // Wind speed
                    
                    // Struct for float convertion
                    union{
                        float f;
                        struct{
                            uint8_t v0, v1, v2, v3;
                        } bytes;
                    } floatConvertion;
                    
                    floatConvertion.f = windSpeed;
                    Buffer[5] = floatConvertion.bytes.v0;
                    Buffer[6] = floatConvertion.bytes.v1;
                    Buffer[7] = floatConvertion.bytes.v2;
                    Buffer[8] = floatConvertion.bytes.v3;
                    
                    // Water
                    // Convertion
                    floatConvertion.f = water * 25.4f;
                    Buffer[9] = floatConvertion.bytes.v0;
                    Buffer[10] = floatConvertion.bytes.v1;
                    Buffer[11] = floatConvertion.bytes.v2;
                    Buffer[12] = floatConvertion.bytes.v3;
                    
                    // Battery
                    floatConvertion.f = battery;
                    Buffer[13] = floatConvertion.bytes.v0;
                    Buffer[14] = floatConvertion.bytes.v1;
                    Buffer[15] = floatConvertion.bytes.v2;
                    Buffer[16] = floatConvertion.bytes.v3;
                    
                    // Pressure
                    floatConvertion.f = pressure;
                    Buffer[17] = floatConvertion.bytes.v0;
                    Buffer[18] = floatConvertion.bytes.v1;
                    Buffer[19] = floatConvertion.bytes.v2;
                    Buffer[20] = floatConvertion.bytes.v3;
                    Buffer[21] = 42; //Marker for a weather packet
                State = TX;
                Radio.SetChannel( RF_FREQUENCYSEND );
                Radio.Tx( TX_TIMEOUT_VALUE );
}
void OnRxError( void )
{
    Radio.Sleep( );
    State = RX_ERROR;
    debug_if( DEBUG_MESSAGE, "> OnRxError\n\r" );
    pc.printf("RX ERROR");
    sending_weather_values = true;
}

void OnTxDone( void )
{
    Radio.Sleep( );
    //State = TX;
    debug_if( DEBUG_MESSAGE, "> OnTxDone\n\r" );
    Radio.SetChannel( RF_FREQUENCYRECEIVE );
    State = RX;
}
void OnTxTimeout( void )
{
    Radio.Sleep( );
    State = TX_TIMEOUT;
    debug_if( DEBUG_MESSAGE, "> OnTxTimeout\n\r" );
    pc.printf("\n TIMEOUT TX \n");
}
