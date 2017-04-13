/*
 * $QNXLicenseC: 
 * Copyright 2008, QNX Software Systems.  
 *  
 * Licensed under the Apache License, Version 2.0 (the "License"). You  
 * may not reproduce, modify or distribute this software except in  
 * compliance with the License. You may obtain a copy of the License  
 * at: http://www.apache.org/licenses/LICENSE-2.0  
 *  
 * Unless required by applicable law or agreed to in writing, software  
 * distributed under the License is distributed on an "AS IS" basis,  
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied. 
 * 
 * This file may contain contributions from others, either as  
 * contributors under the License or as licensors under other terms.   
 * Please review this entire file for other proprietary rights or license  
 * notices, as well as the QNX Development Suite License Guide at  
 * http://licensing.qnx.com/license-guide/ for other information. 
 * $ 
 */







#include <dlfcn.h>
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include "dl.h"

static int
is_bound(const struct dll_list *test) {
	const struct dll_list	*l;

	for (l = dll_list; l->fname != NULL; ++l) {
		if (l == test) return(1);
	}
	return(0);
}

void *
pci_dlopen(const char *pathname, int mode) {
	const struct dll_list	*l;
	for (l = dll_list; l->fname != NULL; ++l) {
		if (!strcmp(l->fname, pathname)) return((void *)l);
	}
	return(dlopen(pathname, mode));
}

void *
pci_dlsym(void *handle, const char *name) {
	const struct dll_syms	*s;

	if (!is_bound(handle)) return(dlsym(handle, name));

	for (s = ((struct dll_list *)handle)->syms; s->symname != NULL; ++s) {
		if (!strcmp(name, s->symname)) return(s->addr);
	}
	return(errno = ENOENT, (void *)NULL);
}

int
pci_dlclose(void *handle) {
	if (!is_bound(handle)) return(dlclose(handle));

	return(0);
}

char *
pci_dlerror(void) {
	return(strerror(errno));
}

__SRCVERSION( "$URL: http://svn/product/tags/internal/bsp/nto650/ti-j5-evm/1.0.0/latest/hardware/pci/dl.c $ $Rev: 655782 $" );
