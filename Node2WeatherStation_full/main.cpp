/**
 * This is an iRock, the core components of the projects
 * - gathers sensors activated information and passes the packet to a weather station.
 * - encrypts data with AES (not implemented yet)
 */

#include "mbed.h"
#include "main.h"
#include "sx1276-hal.h"
#include "debug.h"

/* Set this flag to '1' to display debug messages on the console */
#define DEBUG_MESSAGE   1

/* Set this flag to '1' to use the LoRa modulation or to '0' to use FSK modulation */
#define USE_MODEM_LORA  1
#define USE_MODEM_FSK   !USE_MODEM_LORA

#define RF_FREQUENCY                                   868200000 // Hz

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

SX1276MB1xAS Radio( OnTxDone, OnTxTimeout,  OnRxDone, OnRxTimeout, OnRxError, NULL, NULL );


uint16_t BufferSize = BUFFER_SIZE;
uint8_t Buffer[BUFFER_SIZE];

int16_t RssiValue = 0.0;
int8_t SnrValue = 0.0;

// For the serial commnication
Serial pc(USBTX, USBRX);

int main() 
{
    uint8_t i;
    debug( "\n\n\rSX1276 iROCK TX\n\n\r" );
    
    // verify the connection with the board
    while( Radio.Read( REG_VERSION ) == 0x00  )
    {
        debug( "Radio could not be detected!\n\r", NULL );
        wait( 1 );
    }
    Radio.SetChannel( RF_FREQUENCY );         
    debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1276MB1LAS ) ) , "\n\r > Board Type: SX1276MB1LAS < \n\r" );
    debug_if( ( DEBUG_MESSAGE & ( Radio.DetectBoardType( ) == SX1276MB1MAS ) ) , "\n\r > Board Type: SX1276MB1MAS < \n\r" );
    
    Radio.SetChannel( RF_FREQUENCY ); 

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
     
    debug_if( DEBUG_MESSAGE, "Starting Node to Weather Station loop\r\n" ); 
        
    led = 0;
        
    Radio.Tx( TX_TIMEOUT_VALUE );
    bool isF4 = true;
    while(1){
        switch( State )
        {
         case TX:
            if( BufferSize > 0 )
                { 
                    int x = 0; //buffer size index
                    led = !led; 
                    debug("\n TX mode ectiveted \r\n");
                    /*
                     * Preparing data
                     * Each slot in Buffer is uint8_t (8 bytes)                     
                     */
                    uint8_t id = 155; //Unique ID for a node
                    if(id<0 || id>255){
                        debug( "Choose an ID between 0 and 255!!! \n\r", NULL );
                        return 0; 
                    }
                    if(isF4){
                        if(id<=100){
                            debug( "If F4, choose an ID larger than 100!! \n\r", NULL );
                            return 0;
                        }
                    }
                    else{
                        if(id>100){
                            debug( "If F3, choose an ID smaller than 100!! \n\r", NULL );
                        }
                    }
                    
                    int16_t rssi_value = Radio.GetRssi(MODEM_LORA);
                                       
                        
                        // Putting the unique ID of node in buffer
                        Buffer[0] = id;
                            
                        // RSSI Value splitted into 2 slots of 8 bytes each    
                        union{
                            int16_t i;
                            struct{
                                int8_t v0, v1;
                            } bytes;
                        } rssiToSend;
                        
                        rssiToSend.i = rssi_value;
                        
                        Buffer[1] = rssiToSend.bytes.v0;
                        Buffer[2] = rssiToSend.bytes.v1;
                        
                    if(isF4){
                            int16_t accel_x = rand() % 7 + (-3);
                            int16_t accel_y = rand() % 7 + (-3);
                            int16_t accel_z = rand() % 7 + (-3);
                        // Accelerometer values splitted into 2 slots of 8 bytes too
                        union{
                            int16_t i;
                            struct{
                                uint8_t v0, v1;
                            } bytes;
                        } accel_x_ToSend;
                        
                        accel_x_ToSend.i = accel_x;
                        
                        Buffer[3] = accel_x_ToSend.bytes.v0;
                        Buffer[4] = accel_x_ToSend.bytes.v1;
                        
                        union{
                            int16_t i;
                            struct{
                                uint8_t v0, v1;
                            } bytes;
                        } accel_y_ToSend;
                        
                        accel_y_ToSend.i = accel_y;
                        
                        Buffer[5] = accel_y_ToSend.bytes.v0;
                        Buffer[6] = accel_y_ToSend.bytes.v1;
                        
                        union{
                            int16_t i;
                            struct{
                                uint8_t v0, v1;
                            } bytes;
                        } accel_z_ToSend;
                        
                        accel_z_ToSend.i = accel_z;
                        
                        Buffer[7] = accel_z_ToSend.bytes.v0;
                        Buffer[8] = accel_z_ToSend.bytes.v1;
                        x = 9;
                    }
                    else{
                        uint16_t compassDegree = rand() % 360;
                        union{
                            uint16_t i;
                            struct{
                                uint8_t v0, v1;
                            } bytes;
                        } compassDegree_ToSend;
                        compassDegree_ToSend.i = compassDegree;
                        Buffer[3] = compassDegree_ToSend.bytes.v0;
                        Buffer[4] = compassDegree_ToSend.bytes.v1;
                        x = 5;
                    }
                    // We fill the buffer with numbers for the payload 
                    int y = 0;
                    for( y = x;  y< BufferSize; y++ )
                    {
                        Buffer[y] = y - BufferSize;
                    }
                    wait_ms( 10 );  
                    Radio.Send( Buffer, BufferSize );
                    pc.printf("\n After sending \n");
                }
                /*
                Radio.Rx(RX_TIMEOUT_VALUE);
                State = RX;
                */
        case TX_TIMEOUT:
            Radio.Tx( TX_TIMEOUT_VALUE );
            State = LOWPOWER;
            break;
        case LOWPOWER:
            break;
        case RX : 
            pc.printf("i got the weather shits!! ");
            if( BufferSize > 0 ) {
                    led=!led;
                    Radio.Rx( RX_TIMEOUT_VALUE );
            }
            break;
        default:
            State = LOWPOWER;
            break;
        }    
    }    
}

void OnTxDone( void )
{
    Radio.Sleep( );
    wait_ms(5000);
    State = TX;
    debug_if( DEBUG_MESSAGE, "> OnTxDone\n\r" );
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
    pc.printf("\n %d ___________\n ", payload[0]);
    /*
    for(char i=0; i<0x08; i++) pc.printf("%c",payload[i]);
    State = TX;
    Radio.Tx(TX_TIMEOUT_VALUE);
    pc.printf("U received the ACK, congrats!!");
    */
}

void OnTxTimeout( void )
{
    Radio.Sleep( );
    State = TX_TIMEOUT;
    debug_if( DEBUG_MESSAGE, "> OnTxTimeout\n\r" );
}

void OnRxTimeout( void )
{
    Radio.Sleep();
    Buffer[ BufferSize ] = 0;
    State = RX_TIMEOUT;
    debug_if( DEBUG_MESSAGE, "> OnRxTimeout\n\r" );
}

void OnRxError( void )
{
    Radio.Sleep( );
    State = RX_ERROR;
    debug_if( DEBUG_MESSAGE, "> OnRxError\n\r" );
}

