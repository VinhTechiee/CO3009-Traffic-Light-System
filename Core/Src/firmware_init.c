/* newlib calls this legacy hook inside __libc_init_array. The CMSIS reset
 * handler initializes memory and invokes __libc_init_array before main. */
void _init(void)
{
}
