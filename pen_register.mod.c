#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xa3ef6c50, "module_layout" },
	{ 0x1837e804, "usb_deregister" },
	{ 0xa1b4eb92, "usb_register_driver" },
	{ 0x27e1a049, "printk" },
	{ 0xb4390f9a, "mcount" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("usb:v0781p5406d*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:vABCDp1234d*dc*dsc*dp*ic*isc*ip*");

MODULE_INFO(srcversion, "D068896F40C6B840C21163B");
