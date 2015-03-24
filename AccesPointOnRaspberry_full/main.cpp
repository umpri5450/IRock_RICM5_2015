/*
Code from https://developer.mbed.org/users/GregCr/code/SX1276PingPong/
Modify by Rodolphe Fréby & Jérôme Barbier to do a point to point communication
*/

#include "mbed.h"
#include "main.h"
#include "sx1276-hal.h"
#include "debug.h"
#include "Crypto.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

//#include "polarssl/aes.h"


unsigned char myKEY[0x20]= {2,1,3,'D',1,8,9,4,8,'D',3,8,2,'F',1,9,4,9,'A',5,7,'F',8,8,4,4,4,1,'D'}; //256bit key
AES myAES(AES_256, myKEY); // will use ECB mode

/* Set this flag to '1' to display debug messages on the console */
#define DEBUG_MESSAGE   0

#define MY_ID 111


/* Set this flag to '1' to use the LoRa modulation or to '0' to use FSK modulation */
#define USE_MODEM_LORA  1
#define USE_MODEM_FSK   !USE_MODEM_LORA

#define RF_FREQUENCY                                    869200000 // Hz
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

int main()
{
    
    debug( "\n\n\r Point to point Receiver \n\n\r" );
    
    // verify the connection with the board
    while( Radio.Read( REG_VERSION ) == 0x00  ) {
        debug( "Radio could not be detected!\n\r", NULL );
        wait( 1 );
    }
    
    debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1276MB1LAS ) ) , "nr > Board Type: SX1276MB1LAS < nr" );
    debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1276MB1MAS ) ) , "nr > Board Type: SX1276MB1MAS < nr" );
    
    Radio.SetChannel( RF_FREQUENCY );
    
    #if USE_MODEM_LORA == 1
    
    debug_if( LORA_FHSS_ENABLED, "nnr             > LORA FHSS Mode < nnr");
    debug_if( !LORA_FHSS_ENABLED, "nnr             > LORA Mode < nnr");
    
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
    
    debug("nnr              > FSK Mode < nnr");
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
            case RX:
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
            Radio.Rx( RX_TIMEOUT_VALUE );
            State = LOWPOWER;
            break;
            
            case LOWPOWER:
            break;
            
            default:
            State = LOWPOWER;
            break;
        }
    }
}


void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    Radio.Sleep( );
    //BufferSize = size;
    //memcpy( Buffer, payload, BufferSize );
    RssiValue = rssi;
    SnrValue = snr;
    State = RX;
    debug_if( DEBUG_MESSAGE, "> OnRxDonenr" );
    pc.printf("Payload 0%d", payload[0]);
    pc.printf("\n Payload[21] : %d \n", payload[21]);
    if(payload[21] == 42){
        pc.printf("\n It's a weather packet \n");
        union{
            uint16_t i;
            struct{
                uint8_t v0, v1;
            } bytes;
        } windToReceive;
        
        windToReceive.bytes.v0=payload[1];
        windToReceive.bytes.v1=payload[2];
        
        uint16_t windS1 = windToReceive.i;
        
        int humidity = payload[3];
        
        int8_t temperature = (int8_t) payload[4];
        
        union{
            float f;
            struct{
                uint8_t v0, v1, v2, v3;
            } bytes;
        } floatConvertion;
        
        floatConvertion.bytes.v0 = payload[5];
        floatConvertion.bytes.v1 = payload[6];
        floatConvertion.bytes.v2 = payload[7];
        floatConvertion.bytes.v3 = payload[8];
        
        float windS2 = floatConvertion.f;
        
        floatConvertion.bytes.v0 = payload[9];
        floatConvertion.bytes.v1 = payload[10];
        floatConvertion.bytes.v2 = payload[11];
        floatConvertion.bytes.v3 = payload[12];
        
        float water = floatConvertion.f;
        
        floatConvertion.bytes.v0 = payload[13];
        floatConvertion.bytes.v1 = payload[14];
        floatConvertion.bytes.v2 = payload[15];
        floatConvertion.bytes.v3 = payload[16];
        
        float battery = floatConvertion.f;
        
        floatConvertion.bytes.v0 = payload[17];
        floatConvertion.bytes.v1 = payload[18];
        floatConvertion.bytes.v2 = payload[19];
        floatConvertion.bytes.v3 = payload[20];
        
        float pressure = floatConvertion.f;
        
        pc.printf("1#%d#%d#%d#%d#%d#%d#%d#%d", MY_ID,windS1,humidity,temperature,windS2,water,battery,pressure);
        }else if(payload[0] <100){
        pc.printf("\n It's an F3 \n");
        union{
            uint16_t i;
            struct{
                uint8_t v0, v1;
            }bytes;
        }compass_ToReceive;
        compass_ToReceive.bytes.v0 = payload[3];
        compass_ToReceive.bytes.v1 = payload[4];
        uint8_t relay_id = payload[5];
        uint16_t compassDegrees = compass_ToReceive.i;
        pc.printf("2#%d#%d#%d#%d", payload[0], compassDegrees, relay_id, rssi );
        }else if(payload[0]>=100){
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
        uint8_t relay_id = payload[9];
        int16_t accel_z = accel_z_ToReceive.i;
        // print result on the serial port
        pc.printf("3#%d#%d#%d#%d#%d#%d", payload[0], accel_x, accel_y, accel_z, relay_id, rssi);
        pc.printf("\n It's an F4 \n");
        }else{

        pc.printf("\n Unknown device \n");
    }
    
}

void OnRxTimeout( void )
{
    Radio.Sleep( );
    Buffer[ BufferSize ] = 0;
    State = RX;
    debug_if( DEBUG_MESSAGE, "> OnRxTimeoutnr" );
    pc.printf("TIMEOUT");
}

void OnRxError( void )
{
    Radio.Sleep( );
    State = RX_ERROR;
    debug_if( DEBUG_MESSAGE, "> OnRxErrornr" );
    pc.printf("ERROR");
}

void OnTxDone( void )
{
    Radio.Sleep( );
    State = TX;
    debug_if( DEBUG_MESSAGE, "> OnTxDonenr" );
}
void OnTxTimeout( void )
{
    Radio.Sleep( );
    State = TX_TIMEOUT;
    debug_if( DEBUG_MESSAGE, "> OnTxTimeoutnr" );
}