# dma_driver
A Linux Kernel Module for DMA (work in progress)
Based on code from https://github.com/pijyoi/kernelmodule

To compile kernel module
make all 

To compile just tester.c
make tester

Intention: After setting up the get_user_pages and filling up the driver_params struct with the pointers
to the 2 buffers, DO NOT map to hardware nor write to registers to trigger the DMA.
NOT DONE: The driver will check internally for the dma data ready interrupt from DMA capable hardware, trigger the DMA, unmap, check for another dma data ready, trigger DMA again, unmap, and then set the bufStatus.filled to 1.

Some sort of polling needs to be done on bufStatus? 
