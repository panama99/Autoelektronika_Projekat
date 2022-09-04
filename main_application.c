/*Projekat izradili: Milica Panic EE2/2018 i Nemanja Miljatovic EE160/2018*/
//Avgust 2022.

/* Standard includes. */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

/* Hardware simulator utility functions */
#include "HW_access.h"

//kanali serijske komunikacije
#define COM_CH_0 (0)
#define COM_CH_1 (1)
#define COM_CH_2 (2)

//pun rezervoar:
#define REZ_PUN 50

/* TASK PRIORITIES */
#define	TASK_SERIAL_SEND_PRI		(tskIDLE_PRIORITY + (UBaseType_t)2 ) 
#define TASK_SERIAL_REC_PRI			( tskIDLE_PRIORITY + (UBaseType_t)3 )
#define	SERVICE_TASK_PRI		( tskIDLE_PRIORITY + (UBaseType_t)1 ) //prioritet za servisne taskove, kao sto su taskovi za proracun nivoa i procenta goriva.

/* TASKS: FORWARD DECLARATIONS */
void main_demo(void);

static void PercentageFuelLevel(void* pvParameters);
static void AverageFuelLevel(void* pvParameters);
static void Display7Segment_LEDbar(void* pvParameters);
static void SerialSend_Task0(void* pvParameters);
static void SerialSend_Task1(void* pvParameters);
static void SerialReceive_Task0(void* pvParameters);
static void SerialReceive_Task1(void* pvParameters);

/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
//nivo goriva u procentima
static uint16_t procenti;
//koliko jos moze automobil da se krece
static uint16_t moguca_voznja;
//ocitana otpornost
static uint16_t otpornost;

static uint16_t MINFUEL;
static uint16_t POTROSNJA;
static uint16_t MAXFUEL;

/* RECEPTION DATA BUFFER */
#define R_BUF_SIZE (32)

//brojaci
static uint8_t volatile r_point;
static uint8_t volatile r_point1;

//baferi za redove
static char r_buffer[R_BUF_SIZE];
static char r_buffer1[R_BUF_SIZE];

//baferi za cuvanje minimalne,maximalne vrijednosti otpornosti i potrosnje ocitane preko serisjke.
static char min_resistance[R_BUF_SIZE];
static char max_resistance[R_BUF_SIZE];
static char pp[R_BUF_SIZE];

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const uint8_t hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

/* GLOBAL OS-HANDLES */
static SemaphoreHandle_t RXC_BinarySemaphore0;
static SemaphoreHandle_t RXC_BinarySemaphore1;

//red za slanje ocitanih vrijednosti otpornosti.
static QueueHandle_t QueueResistance;

static void Display7Segment_LEDbar(void* pvParameters)
{
	uint8_t taster1;
	uint16_t d = 10;
	uint16_t nula = '0';

	uint8_t cc;
	uint8_t a = 0;
	uint8_t b = 0;
	uint8_t c = 0;

	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(100));//osvjezavanje 100ms.


		for (;;)
		{
			//racunanje procenta nakon pokretanja START tastera:
			uint16_t jed = procenti % d;
			uint16_t des = (procenti / d) % d;
			uint16_t sto = procenti / (uint16_t)100;
			//---------------------------------------------
			//izvlacenje cifara iz vrijednosti ocitane otpornosti:
			uint16_t jed2 = otpornost % d;
			uint16_t des2 = (otpornost / d) % d;
			uint16_t sto2 = (otpornost / (uint16_t)100) % d;
			uint16_t hiljada = (otpornost / (uint16_t)1000) % d;
			uint16_t dhiljada = otpornost / (uint16_t)10000;


			//procenat ce se ispisivati na prva tri bloka displeja.
			select_7seg_digit(0);
			set_7seg_digit(hexnum[sto]);

			select_7seg_digit(1);
			set_7seg_digit(hexnum[des]);

			select_7seg_digit(2);
			set_7seg_digit(hexnum[jed]);

			//ocitana otpornost ce se prikazivati na zadnja 5 bloka displeja
			select_7seg_digit(9);
			set_7seg_digit(0x50);

			select_7seg_digit(8);
			set_7seg_digit(hexnum[jed2]);

			select_7seg_digit(7);
			set_7seg_digit(hexnum[des2]);

			select_7seg_digit(6);
			set_7seg_digit(hexnum[sto2]);

			select_7seg_digit(5);
			set_7seg_digit(hexnum[hiljada]);

			select_7seg_digit(4);
			set_7seg_digit(hexnum[dhiljada]);
		}

			//osvjezavanje displeja 
			for (uint8_t i = 0; i < 10; i++)
			{
				select_7seg_digit((uint8_t)i);
				set_7seg_digit(0x00);
			}

	}
}



//Funkcija koja racuna srednju vrijednost zadnjih 5 ocitavanja otpornosti.
static void AverageFuelLevel(void* pvParameters)
{

	uint16_t rec_buf;
	int brojac = 0;
	uint16_t suma = 0;
	uint16_t prosijek;
	for (;;)
	{

		xQueueReceive(QueueResistance, &rec_buf, portMAX_DELAY);
		suma += rec_buf;
		brojac++;
		if (brojac == 5)
		{
			prosijek = suma / 5;
			printf("* Srednja vrijednost zadnjih 5 ocitavanja: %d *\n", prosijek);
			printf("------------------------------------------\n");

			suma = 0;
			brojac = 0;
		}
	}
}
//Funkcija koja racuna koliko jos automobil moze da predje kilometara na osnovu nivoa goriva.
static void PercentageFuelLevel(void* pvParameters)
{
	uint16_t rec_buf;

	for (;;)
	{
		xQueueReceive(QueueResistance, &rec_buf, portMAX_DELAY);// prijem podatka nivou goriva preko reda.


		if (MINFUEL != (uint16_t)0 && MAXFUEL != (uint16_t)0)
		{
			procenti = (uint16_t)100 * (rec_buf - MINFUEL) / (MAXFUEL - MINFUEL);

			printf("Procenat: %d % \n", procenti);

			if (procenti < (uint16_t)10)
			{
				if (set_LED_BAR(0, 0xff) != 0)//ako je procenat ispod 10% neka svijetli cijeli 3.stupac dioda.
				{
					printf("Problem");
				}
			}
			else
			{
				if (set_LED_BAR(0, 0x80) != 0)//ako je procenat iznad 10% onda svijetli samo prva dioda gore u trecem stupcu.
				{
					printf("Problem");
				}
			}
		}
		if (POTROSNJA != (uint16_t)0 && MINFUEL != (uint16_t)0 && MAXFUEL != (uint16_t)0)
		{
			moguca_voznja = procenti * (uint16_t)REZ_PUN / POTROSNJA;//pun rezervoar je 50l, potrosnja je koliko trosimo l na npr.100km.

			printf("Auto moze jos da vozi:  %d km\n", moguca_voznja);
		}
	}
}


//Funkcija koja ucitava vrijednosti otpornosti u red.
static void SerialReceive_Task0(void* pvParameters)
{

	uint8_t cc;

	for (;;)
	{
		xSemaphoreTake(RXC_BinarySemaphore0, portMAX_DELAY);
		

		get_serial_character(COM_CH_0, &cc);
		

		//kada stignu podaci, salju se u red
		if (cc == (uint8_t)'V')
		{
			r_point = 0;
		}
		else if (cc == (uint8_t)'R')
		{
			char* ostatak;


			otpornost = (uint16_t)strtol(r_buffer, &ostatak, 10);

			xQueueSend(QueueResistance, &otpornost, 0);
			printf(" Otpornost je %s\n", r_buffer);

			r_buffer[0] = '\0';
			r_buffer[1] = '\0';
			r_buffer[2] = '\0';
			r_buffer[3] = '\0';
			r_buffer[4] = '\0';
		}
		else
		{
			r_buffer[r_point++] = (char)cc;
		}




	}
}
//Funkcija koja omogucava slanje trigera T koji ce aktivirati ocitavanje otpornosti
static void SerialSend_Task0(void* pvParameters)
{
	uint8_t c = (uint8_t)'T';

	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
		if (send_serial_character(COM_CH_0, c) != 0)
		{
			printf("Greska prilikom slanja");
		}
	}
}


//Task koji racuna nivo goriva u procentima.
static void SerialSend_Task1(void* pvParameters)
{
	uint8_t proc[5];
	static uint8_t brojac = 0;
	uint8_t d = 10;
	uint8_t nula = '0';


	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));

		uint8_t jed = procenti % d;//primjer: 567%10 = 7.
		uint8_t des = (procenti / d) % d;//6
		uint8_t sto = procenti / (uint16_t)100;//5

		proc[0] = sto + nula;
		proc[1] = des + nula;
		proc[2] = jed + nula;
		proc[3] = (uint8_t)'%';

		proc[4] = (uint8_t)'\n';

		brojac++;

		if (MINFUEL != (uint16_t)0 && MAXFUEL != (uint16_t)0)
		{
			if (brojac == (uint8_t)1)
			{
				if (send_serial_character(COM_CH_1, proc[0]) != 0)
				{
					printf("Greska");

				}

			}
			else if (brojac == (uint8_t)2)
			{
				if (send_serial_character(COM_CH_1, proc[1]) != 0)
				{
					printf("Greska");
				}
			}
			else if (brojac == (uint8_t)3)
			{
				if (send_serial_character(COM_CH_1, proc[2]) != 0)
				{
					printf("Greska");
				}
			}
			else if (brojac == (uint8_t)4)
			{
				if (send_serial_character(COM_CH_1, proc[3]) != 0)
				{
					printf("Greska");
				}
			}
			else if (brojac == (uint8_t)5)
			{
				if (send_serial_character(COM_CH_1, proc[4]) != 0)
				{
					printf("Greska");
				}
			}
			else
			{
				brojac = (uint8_t)0;
			}
		}
	}
}

static void SerialReceive_Task1(void* pvParameters)
{
	uint8_t cc;

	uint8_t a = 0;
	uint8_t b = 0;
	uint8_t c = 0;


	for (;;)
	{
		xSemaphoreTake(RXC_BinarySemaphore1, portMAX_DELAY);

		if (get_serial_character(COM_CH_1, &cc) != 0)
		{
			printf("Neuspjesno");
		}

		if (cc == (uint8_t)0x00)//akosmo na pocetku, detektujemo nule, resetujemo brojace.
		{
			a = 0;
			b = 0;
			c = 0;

			r_point1 = 0;
		}
		else if (cc == (uint8_t)13) //13 je 0d, kraj poruke.)
		{
			if (r_buffer1[0] == 'M' && r_buffer1[1] == 'I' && r_buffer1[2] == 'N')//provjera da li su uhvacena slova MIN.
			{

				for (uint16_t i = (uint16_t)0; i < strlen(r_buffer1); i++)
				{

					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						min_resistance[b] = r_buffer1[i];//u min_resistance se ubacuje MIN otpornost koju smo ocitale sa serijske.
						b++;
					}
				}
			}
			else if (r_buffer1[0] == 'M' && r_buffer1[1] == 'A' && r_buffer1[2] == 'X')
			{
				for (uint16_t i = (uint16_t)0; i < strlen(r_buffer1); i++)
				{

					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						max_resistance[c] = r_buffer1[i];
						c++;
					}
				}
			}
			else if (r_buffer1[0] == 'P' && r_buffer1[1] == 'P')
			{
				for (uint16_t i = (uint16_t)0; i < strlen(r_buffer1); i++)
				{

					if (r_buffer1[i] == '0' || r_buffer1[i] == '1' || r_buffer1[i] == '2' || r_buffer1[i] == '3' || r_buffer1[i] == '4' || r_buffer1[i] == '5' || r_buffer1[i] == '6' || r_buffer1[i] == '7' || r_buffer1[i] == '8' || r_buffer1[i] == '9')
					{
						pp[a] = r_buffer1[i];
						a++;
					}
				}
			}

			vTaskDelay(pdMS_TO_TICKS(500));

			char* p;

			MINFUEL = (uint16_t)strtol(min_resistance, &p, 10);//pretvorimo iz stringa u long int kako bismo mogli manipulisati sa vrijednostima.
			MAXFUEL = (uint16_t)strtol(max_resistance, &p, 10);


			printf("MIN: %d\n", MINFUEL);
			printf("MAX: %d\n", MAXFUEL);

			POTROSNJA = (uint16_t)strtol(pp, &p, 10);//konverzija string long integer.
			printf("Potrosnja: %d\n", POTROSNJA);

			r_buffer1[0] = '\0';
			r_buffer1[1] = '\0';
			r_buffer1[2] = '\0';
			r_buffer1[3] = '\0';
			r_buffer1[4] = '\0';
			r_buffer1[5] = '\0';
			r_buffer1[6] = '\0';
			r_buffer1[7] = '\0';
			r_buffer1[8] = '\0';
			r_buffer1[9] = '\0';
			r_buffer1[10] = '\0';
			r_buffer1[11] = '\0';
		}
		else
		{
			r_buffer1[r_point1++] = (char)cc;
		}
	}
}
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t higher_priority_task_woken = pdFALSE;

	xSemaphoreGiveFromISR(RXC_BinarySemaphore0, &higher_priority_task_woken);

	xSemaphoreGiveFromISR(RXC_BinarySemaphore1, &higher_priority_task_woken);

	portYIELD_FROM_ISR((uint32_t)higher_priority_task_woken);

}

void main_demo(void)
{
	init_7seg_comm();
	//Inicijalizacija serijske komunikacije za kanal 0 i 1.
	if (init_LED_comm() != 0)
	{
		printf("Neuspjesno");
	}

	if (init_serial_uplink(COM_CH_0) != 0)
	{
		printf("Neuspjesno");
	}
	if (init_serial_downlink(COM_CH_0) != 0)
	{
		printf("Neuspjesno");
	}
	if (init_serial_uplink(COM_CH_1) != 0)
	{
		printf("Neuspjesno");
	}
	if (init_serial_downlink(COM_CH_1) != 0)
	{
		printf("Neuspjesno");
	}
	if (init_serial_uplink(COM_CH_2) != 0)
	{
		printf("Neuspjesno");
	}
	if (init_serial_downlink(COM_CH_2) != 0)
	{
		printf("Neuspjesno");
	}


	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	//Kreiranje semafora:

	RXC_BinarySemaphore0 = xSemaphoreCreateBinary();
	if (RXC_BinarySemaphore0 == NULL)
	{
		printf("Greska kreiranja semafora\n");
	}

	RXC_BinarySemaphore1 = xSemaphoreCreateBinary();
	if (RXC_BinarySemaphore0 == NULL)
	{
		printf("Greska kreiranja semafora\n");
	}

	//Kreiranje redova:

	QueueResistance = xQueueCreate(10, sizeof(uint16_t));
	if (QueueResistance == NULL)
	{
		printf("Greska kreiranja reda\n");
	}


	//Kreiranje taskova:

	BaseType_t status;
	status = xTaskCreate(AverageFuelLevel, "avg_value", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)SERVICE_TASK_PRI, NULL);

	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	status = xTaskCreate(SerialReceive_Task0, "SRx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)TASK_SERIAL_REC_PRI, NULL);
	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	status = xTaskCreate(SerialSend_Task0, "STx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)TASK_SERIAL_SEND_PRI, NULL);
	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	status = xTaskCreate(SerialReceive_Task1, "SRx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)TASK_SERIAL_REC_PRI, NULL);
	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	status = xTaskCreate(SerialSend_Task1, "STx", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)TASK_SERIAL_SEND_PRI, NULL);
	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	status = xTaskCreate(PercentageFuelLevel, "per_value", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)SERVICE_TASK_PRI, NULL);
	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	status = xTaskCreate(Display7Segment_LEDbar, "display", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)SERVICE_TASK_PRI, NULL);
	if (status != pdPASS)
	{
		printf("Greska task kreiranja\n");
	}

	r_point1 = 0;
	r_point = 0;



	vTaskStartScheduler();

}
