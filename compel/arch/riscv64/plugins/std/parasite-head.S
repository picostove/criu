#include "common/asm/linkage.h"

	.section .head.text, "ax"
ENTRY(__export_parasite_head_start)
	call	parasite_service
	ebreak
END(__export_parasite_head_start)
