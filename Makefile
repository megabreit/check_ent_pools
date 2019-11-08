# Makefile for check_ent_pools and friends
#
# gcc tested working on AIX>5.3 TL12 SP5, need to make sure to run the gcc for the
# correct AIX version (gcc for AIX 5.3 will NOT work on AIX 6.1 and stop
# with errors in various system include files)
#
#CC=gcc
# 
CC=/usr/vac/bin/xlc

LIBS=-lperfstat

all:	check_ent_pools check_entitlement check_cpu_pools

check_ent_pools: check_ent_pools.c
	$(CC) $(LIBS) check_ent_pools.c -o $@

check_entitlement: check_entitlement.c
	$(CC) $(LIBS) check_entitlement.c -o $@

check_cpu_pools: check_cpu_pools.c
	$(CC) $(LIBS) check_cpu_pools.c -o $@

clean:
	rm -f check_ent_pools check_entitlement check_cpu_pools
