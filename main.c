/*******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the comparator based wakeup from hibernate
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* $ Copyright 2023 Cypress Semiconductor $
*******************************************************************************/
/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cyhal.h"
#include "cybsp.h"
#include "cy_pdl.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define MY_LPCOMP_ULP_SETTLE        (50u)
#define MY_LPCOMP_OUTPUT_LOW        (0u)
#define MY_LPCOMP_OUTPUT_HIGH       (1u)
#define TOGGLE_LED_PERIOD           (500u)
#define LED_ON_DUR_BEFORE_HIB_IN_MS (2000u)
#define CYHAL_HYSTERESIS_DISABLE    (0u)
#define PIN_VINP                     P5_6
#define PIN_VINM                     P5_7
/*******************************************************************************
* Global Variables
*******************************************************************************/

/* LPCOMP configuration structure */
static cyhal_comp_config_t myCompConfig =
{
    .power = CYHAL_POWER_LEVEL_LOW,
    .hysteresis = CYHAL_HYSTERESIS_DISABLE,
};


/*******************************************************************************
* Function Prototypes
*******************************************************************************/

/*******************************************************************************
* Function Definitions
*******************************************************************************/


/*******************************************************************************
* Function Name: lpcomp_enter_hibernate_mode
********************************************************************************
* Summary:
* This is the user-defined function for CPU. 
*    1.The function enters into the Hibernation mode. The LED is on 2 seconds before
*  the Hibernation.
*    2.If failed to enter, an error message is displayed as "Not entered Hibernate mode" and CPU seizes.
*
* Parameters:
*  lpcomp_wakeup_src
*
* Return:
*  cyhal_syspm_hibernate_source_t
*
*******************************************************************************/

static void lpcomp_enter_hibernate_mode(cyhal_syspm_hibernate_source_t lpcomp_wakeup_src)
{
    cy_rslt_t status;

    /* Turn on LED for 2 seconds to indicate the MCU entering Hibernate mode. */
    cyhal_gpio_write(CYBSP_USER_LED, CYBSP_LED_STATE_ON);
    cyhal_system_delay_ms(LED_ON_DUR_BEFORE_HIB_IN_MS);
    cyhal_gpio_write(CYBSP_USER_LED, CYBSP_LED_STATE_OFF);
    
    printf("De-initializing IO and entering Hibernate mode, turned On User LED for 2sec \r\n\n");
    
    /*brief Releases the UART interface allowing it to be used for other purposes*/
    cy_retarget_io_deinit();

    /*Set the GPIO Drive mode to High Impedance to eliminate garbage data in UART */
    #ifdef TARGET_APP_CY8CKIT_062S2_43012
    Cy_GPIO_SetDrivemode(GPIO_PRT5, 1, CY_GPIO_DM_HIGHZ);
    #endif

    /* Set the wake-up signal from Hibernate and Jump into Hibernate */
    status = cyhal_syspm_hibernate(lpcomp_wakeup_src);
    if (status != CY_SYSPM_SUCCESS)
    {
        /* Re-Initialize retarget-io to use the debug UART port */
        cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
        printf("Not entered Hibernate mode\r\n\n");
        CY_ASSERT(0);
    }
}
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CPU. It 
*    1.Switches to Hibernate mode when LP comparison result is lower than Vref voltage.
*    2.Toggles LED at 500ms when LP comparison result is higher than Vref voltage.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    cyhal_comp_t myComp;

#if defined (CY_DEVICE_SECURE)
    cyhal_wdt_t wdt_obj;

    /* Clear watchdog timer so that it doesn't trigger a reset */
    result = cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    cyhal_wdt_free(&wdt_obj);
#endif

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();


    /* Initialize retarget-io to use the debug UART port */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
    /* retarget-io init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }  
    
    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    /*Display CE title in the terminal window */
    printf("****************** "
           "Low-power-comp-hibernate-wakeup "
           "****************** \r\n\n");

    /* Initialize comparator, using pin PIN_VINP for the input and pin PIN_VINM for the reference.
    * No output pin is used
    */
    cyhal_comp_init(&myComp, PIN_VINP, PIN_VINM, NC, &myCompConfig);

    /* Connect the local reference generator output to the comparator negative input.*/
    Cy_LPComp_ConnectULPReference(LPCOMP, CY_LPCOMP_CHANNEL_0);
    
    /* Enable the local reference voltage */
    Cy_LPComp_UlpReferenceEnable(LPCOMP);

    /* Low comparator power and speed */
    cyhal_comp_set_power(&myComp, CYHAL_POWER_LEVEL_LOW);

    /* It needs 50us start-up time to settle in ULP mode after the block is enabled */
    cyhal_system_delay_us(MY_LPCOMP_ULP_SETTLE);

    /* Initialize USER LED pin GPIO as an output with strong drive mode and initial value = false (low) */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);


    for (;;)
    {
        /* If the comparison result is high, toggles LED every 500ms */
        if (MY_LPCOMP_OUTPUT_HIGH == cyhal_comp_read(&myComp))
        {
            /* Toggle LED every 500ms */
            cyhal_gpio_toggle(CYBSP_USER_LED);
            cyhal_system_delay_ms(TOGGLE_LED_PERIOD);
            printf("In Normal mode, blinking User LED at 500ms \r\n\n");
        }

        /* If the comparison result is low, goes to the hibernate mode */
        else
        {
            /* System wakes up when LPComp channel 0 output is high */
            lpcomp_enter_hibernate_mode(CYHAL_SYSPM_HIBERNATE_LPCOMP0_HIGH);
        }
    }
}

/* [] END OF FILE */
