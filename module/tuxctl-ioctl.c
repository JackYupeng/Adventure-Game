/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)
	
#define PUSH_BUFFER	6
#define WHICH_LED	0x000F0000
#define ONE_BYTE	4
#define TWO_BYTE	8
#define THREE_BYTE	12
#define FOUR_BYTE	16
#define BUTTON_MASK1	0xF0
#define BUTTON_MASK2	0x0F
#define LED_MASK1	0x1
#define LED_MASK2	0xF
#define LED1	0x2
#define LED2	0x4
#define LED3	0x8
#define LED0_SHIFT	24
#define LED1_SHIFT	25
#define LED2_SHIFT	26
#define LED3_SHIFT	27
#define VAL_MASK	0xFF
#define LAYER0	0
#define LAYER1	1
#define LAYER2	2
#define LAYER3	3
#define LAYER4	4
#define LAYER5	5
#define VAL0	0x000000F7
#define VAL1	0x00000016
#define VAL2	0x000000DB
#define VAL3	0x0000009F
#define VAL4	0x0000003E
#define VAL5	0x000000BD
#define VAL6	0x000000FD
#define VAL7	0x00000096
#define VAL8	0x000000FF
#define VAL9	0x000000BE
#define VALA	0x000000FE
#define VALB	0x0000007D
#define VALC	0x000000F1
#define VALD	0x0000005F
#define VALE	0x000000F9
#define VALF	0x000000F8

#define VALL1	0x000000E7
#define VALL2	0x00000006
#define VALL3	0x000000CB
#define VALL4	0x0000008F
#define VALL5	0x0000002E
#define VALL6	0x000000AD
#define VALL7	0x000000ED
#define VALL8	0x00000086
#define VALL9	0x000000EF
#define VALL10	0x000000AE
#define VALLA	0x000000EE
#define VALLB	0x0000006D
#define VALLC	0x000000E1
#define VALLD	0x0000004F
#define VALLE	0x000000E9	
#define VALLF	0x000000E8




	
//global variables below
	//static unsigned long led_state;	//keeps track of the LED status
	uint8_t button;
	unsigned char buffer[6];	//declare a buffer that of size 6 to hold the LED bits
	unsigned char bioc;
	unsigned char led;
	
	unsigned int flag = 0;
	
	static spinlock_t button_lock = SPIN_LOCK_UNLOCKED;
	static spinlock_t led_lock = SPIN_LOCK_UNLOCKED;
	static spinlock_t flag_lock = SPIN_LOCK_UNLOCKED;


int tuxctl_ioctl_tux_set_led(struct tty_struct *tty, unsigned long arg);
//helper function to set LED
unsigned long determin_val(unsigned long val, unsigned long dp);

/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)	
{
    unsigned a, b, c;
	unsigned long flags;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

    /*printk("packet : %x %x %x\n", a, b, c); */
	switch(a){
		case MTCP_ACK:	//set flag here
			spin_lock_irqsave(&flag_lock, flags);
			flag = 0;
			spin_unlock_irqrestore(&flag_lock, flags);
			return;
			
		case MTCP_BIOC_EVENT:	//check button status
			spin_lock_irqsave(&button_lock, flags);
			button = ((c<<ONE_BYTE)&BUTTON_MASK1) | (b&BUTTON_MASK2);
			spin_unlock_irqrestore(&button_lock, flags);
			return;
			
		case MTCP_RESET:	//reset the game to initial state
			bioc = MTCP_BIOC_ON;
			led = MTCP_LED_USR;
			tuxctl_ldisc_put(tty, &bioc, 1);	//reset button
			tuxctl_ldisc_put(tty, &led, 1);		//reset LED
			
			spin_lock_irqsave(&flag_lock, flags);
			if(flag==1){
				spin_unlock_irqrestore(&flag_lock, flags);
				return;
			}
			spin_unlock_irqrestore(&flag_lock, flags);
			
			spin_lock_irqsave(&led_lock, flags);
			tuxctl_ldisc_put(tty, buffer, PUSH_BUFFER);	//set the LED
			spin_unlock_irqrestore(&led_lock, flags);
			return;	
		default:
			return;	
	}
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 	
	      unsigned cmd, unsigned long arg)
{
	unsigned long flags;
	
    switch (cmd) {
		case TUX_INIT:
			bioc = MTCP_BIOC_ON;
			led = MTCP_LED_USR;
			tuxctl_ldisc_put(tty, &bioc, 1);
			tuxctl_ldisc_put(tty, &led, 1);
			return 0;
		case TUX_BUTTONS:
			if(arg==0){
				return -EINVAL;
			}
			spin_lock_irqsave(&button_lock, flags);
			copy_to_user((void*)arg, &button, 1);	//copy to user
			spin_unlock_irqrestore(&button_lock, flags);
			return 0;
		case TUX_SET_LED:
			return tuxctl_ioctl_tux_set_led(tty, arg);	//set the led
		default:
			return -EINVAL;
    }
}


/*
 * tuxctl_ioctl_tux_set_led
 *   DESCRIPTION: Set the led
 *   INPUTS: none
 *   OUTPUTS: 0
 *   SIDE EFFECTS: Enable the LED to be turned on and off
 */
int tuxctl_ioctl_tux_set_led(struct tty_struct *tty, unsigned long arg){	
	
	unsigned long which_led;
	unsigned long led0, led1, led2, led3;
	unsigned long dp_temp;
	uint8_t val_on_buf;
	unsigned long flags;
	
	buffer[LAYER0] = MTCP_LED_SET;	//opcode
	
	which_led = (arg & WHICH_LED);	//which LED should be turned on
	which_led = which_led >> FOUR_BYTE;	//now the lowest four bits
	buffer[LAYER1] = which_led;	

	spin_lock_irqsave(&led_lock, flags);
	if((which_led & LED_MASK1)){	//if LED0 is on
		led0 = (arg & LED_MASK2);	//value of LED0
		dp_temp = ((arg>>LED0_SHIFT)&LED_MASK1);
		val_on_buf = determine_val(led0, dp_temp);	//determine the bits to be written on the buffer for LED0 
		buffer[LAYER2] = val_on_buf&VAL_MASK;	//write the bits on the buffer
		spin_unlock_irqrestore(&led_lock, flags);
	}
	
	spin_lock_irqsave(&led_lock, flags);
	if((which_led & LED1)){	//if LED1 is on 
		led1 = ((arg>>ONE_BYTE) & LED_MASK2);	//value of LED1
		dp_temp = ((arg>>LED1_SHIFT)&LED_MASK1);
		val_on_buf = determine_val(led1, dp_temp);	//determine the bits to be written on the buffer for LED1
		buffer[LAYER3] = val_on_buf&VAL_MASK;	//write the bits on the buffer
		spin_unlock_irqrestore(&led_lock, flags);
	}	
	
	spin_lock_irqsave(&led_lock, flags);
	if((which_led & LED2)){	//if LED2 is on
		led2 = ((arg>>TWO_BYTE) & LED_MASK2);	//value of LED0
		dp_temp = ((arg>>LED2_SHIFT)&LED_MASK1);
		val_on_buf = determine_val(led2, dp_temp);	//determine the bits to be written on the buffer for LED2
		buffer[LAYER4] = val_on_buf&VAL_MASK;	//write the bits on the buffer
		spin_unlock_irqrestore(&led_lock, flags);
	}
	
	spin_lock_irqsave(&led_lock, flags);
	if((which_led & LED3)){	//if LED3 is on
		led3 = ((arg>>THREE_BYTE) & LED_MASK2);	//value of LED0
		dp_temp = ((arg>>LED3_SHIFT)&LED_MASK1);
		val_on_buf = determine_val(led3, dp_temp);	//determine the bits to be written on the buffer for LED3
		buffer[LAYER5] = val_on_buf&VAL_MASK;	//write the bits on the buffer
		spin_unlock_irqrestore(&led_lock, flags);
	}	
	
	spin_lock_irqsave(&flag_lock, flags);
	if(flag == 1){
		spin_unlock_irqrestore(&flag_lock, flags);
		return -EINVAL;
	}
	else{
		flag = 1;
	}
	spin_unlock_irqrestore(&flag_lock, flags);
	

	spin_lock_irqsave(&led_lock, flags);
	tuxctl_ldisc_put(tty,buffer,6);	//Write bytes out to the device
	spin_unlock_irqrestore(&led_lock, flags);
	return 0;
	
	
	
}

/*
 * determine_val
 *   DESCRIPTION: Helper function for set led. Determin the bits for each LED
 *   INPUTS: Value to be displayed for each LED, decimal point val
 *   OUTPUTS: Bits set for each LED
 *   SIDE EFFECTS: None
 */
unsigned long determine_val(unsigned long val, unsigned long dp_temp){	

	unsigned long flags;
	
	spin_lock_irqsave(&led_lock, flags);
	if(dp_temp == 1){	//if decimal point is 1
		switch(val){
			case(0x0):	//value is 0
				val = VAL0;
				break;
			case(0x1):	//value is 1
				val = VAL1;
				break;
			case(0x2):	//value is 2
				val = VAL2;
				break;
			case(0x3):	//value is 3
				val = VAL3;
				break;
			case(0x4):	//value is 4
				val = VAL4;
				break;
			case(0x5):	//value is 5
				val = VAL5;
				break;
			case(0x6):	//value is 6
				val = VAL6;
				break;
			case(0x7):	//value is 7
				val = VAL7;
				break;
			case(0x8):	//value is 8
				val = VAL8;
				break;
			case(0x9):	//value is 9
				val = VAL9;
				break;
				
			case(0xA):	//value is A
				val = VALA;
				break;
			case(0xB):	//value is B
				val = VALB;
				break;
			case(0xC):	//value is C
				val = VALC;
				break;
			case(0xD):	//value is D
				val =VALD;
				break;
			case(0xE):	//value is E
				val = VALE;
				break;
			case(0xF):	//value is F
				val = VALF;
				break;
			default:
				return 0x0;
		}
		spin_unlock_irqrestore(&led_lock, flags);
		return val;
	}
	
	spin_lock_irqsave(&led_lock, flags);
	if(dp_temp == 0){	//if decimal point is 0
		switch(val){
			case(0x0):	//value is 0
				val = VALL1;
				break;
			case(0x1):	//value is 1
				val = VALL2;
				break;
			case(0x2):	//value is 2
				val = VALL3;
				break;
			case(0x3):	//value is 3
				val = VALL4;
				break;
			case(0x4):	//value is 4
				val = VALL5;
				break;
			case(0x5):	//value is 5
				val = VALL6;
				break;
			case(0x6):	//value is 6
				val = VALL7;
				break;
			case(0x7):	//value is 7
				val = VALL8;
				break;
			case(0x8):	//value is 8
				val = VALL9;
				break;
			case(0x9):	//value is 9
				val = VALL10;
				break;
				
			case(0xA):	//value is A
				val = VALLA;
				break;
			case(0xB):	//value is B
				val =VALLB;
				break;
			case(0xC):	//value is C
				val =VALLC;
				break;
			case(0xD):	//value is D
				val =VALLD;
				break;
			case(0xE):	//value is E
				val =VALLE;
				break;
			case(0xF):	//value is F
				val =VALLF;
				break;
			default:
				return 0x0;
		}
		spin_unlock_irqrestore(&led_lock, flags);
		return val;
	}
	spin_unlock_irqrestore(&led_lock, flags);
	return val;
}
