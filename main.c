#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>

int main() {
   
   virConnectPtr hypervisor = virConnectOpen(NULL);
   int isAlive = virConnectIsAlive(hypervisor);
   if (isAlive) {
   	printf("Connection to hypervisor made successfully.");
   }

   /**
    virConnectNumOfDomains gave me the number of guests I spun
    up via uvt-kvm.
    **/
   int numOfDomains = virConnectNumOfDomains(hypervisor);
   printf("\nNumber of domains: %i", numOfDomains);
   printf("\nGetting pointers to all domains...");
   virDomainPtr* allDomains = malloc(numOfDomains * sizeof(virDomainPtr));
   int res = virConnectListAllDomains(hypervisor, &allDomains, 0);
   printf("\n%i pointers added to allDomains array.\n", res);


   virVcpuInfoPtr vCpuInfo = malloc(sizeof(virVcpuInfo));
   unsigned char ** cpuMap = malloc(sizeof(char*));
   int numOfHostCpus = virNodeGetCPUMap(hypervisor, cpuMap, NULL, 0);
   printf("\nFound %i real CPUs on host.\n", numOfHostCpus);

   for(int i = 0;i < numOfDomains; i++) {
   	    printf("\nGetting vCPU info for guest name %s:", virDomainGetName(allDomains[i]));
   		int vCpuRes = virDomainGetVcpus(allDomains[i], vCpuInfo, 3, *cpuMap, sizeof(*cpuMap));
   		printf("\nvCPU Number: %i, vCpu State: %i, vCpu CPU Time (ns): %llu, pCpu Number: %i\n", vCpuInfo->number, vCpuInfo->state, vCpuInfo->cpuTime, vCpuInfo->cpu);

   		unsigned long vMemRes = virDomainGetMaxMemory(allDomains[i]);

   		virDomainMemoryStatStruct stats[15];
   		int memStats = virDomainMemoryStats(allDomains[i], stats, 15, 0);
		printf("\nNumber of stats returned: %i", memStats);
        for(int i = 0; i < memStats; i++) {
        	printf("\nTag: %x\nVal: %lli", stats[i].tag, stats[i].val);
        }

	} 

   
   
   return 0;
}

private int balanceMemory(virConnectPtr hypervisor) {
    /***
        Create map of all guests. What their memory allocation is, 
        what % of it is being used in them. Be able to compare them
        all and choose how to balance physical memory allocation.
    ***/




	return 0;
}

private int balanceCPU(virConnectPtr hypervisor) {
	/****
       Create map of all guest's physical CPU usage.
       Figure out a way to balance such that every pCpu
       handles similar amount of workload.
	****/
	reutrn 0;
}

private int balanceManagedResources(virConnectPtr hypervisor) {
	balanceMemory(hypervisor);
	balanceCPU(hypervisor);
}
