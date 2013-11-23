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
	{ 0xb9ce3a1a, "usb_register_dev" },
	{ 0x4f8b5ddb, "_copy_to_user" },
	{ 0x27e1a049, "printk" },
	{ 0x8c9813d5, "usb_bulk_msg" },
	{ 0x4f6b400b, "_copy_from_user" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x88789e21, "usb_deregister_dev" },
	{ 0xb4390f9a, "mcount" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("usb:v0781p5406d*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:vABCDp1234d*dc*dsc*dp*ic*isc*ip*");

MODULE_INFO(srcversion, "BFF1C47BEB4341A6A47F7FE");
