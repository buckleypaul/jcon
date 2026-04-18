/*
 * hello_jcon — minimal Zephyr sample demonstrating jcon.
 *
 * The putc here hands each byte to printk; ctx is unused. A real integration
 * would typically thread a sink through ctx — e.g. a const struct device *
 * for uart_poll_out, or a byte buffer the caller drains after jcon_end.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <jcon.h>

static int printk_putc(void *ctx, char c)
{
	(void)ctx;
	printk("%c", c);
	return 0;
}

int main(void)
{
	printk("jcon hello sample\n");

	jcon_start(false, printk_putc, NULL);
	jcon_add_string("board", "native_sim");
	jcon_add_uint("uptime_ms", k_uptime_get_32());
	jcon_array_start("features");
		jcon_add_string(NULL, "streaming");
		jcon_add_string(NULL, "zero-heap");
	jcon_array_end();
	jcon_status_t st = jcon_end();

	printk("jcon status: %d\n", (int)st);
	return 0;
}
