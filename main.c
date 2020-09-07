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
   		int res2 = virDomainGetVcpus(allDomains[i], vCpuInfo, 3, *cpuMap, sizeof(*cpuMap));
   		printf("\nvCPU Number: %i, vCpu State: %i, vCpu CPU Time (ns): %llu, pCpu Number: %i\n", vCpuInfo->number, vCpuInfo->state, vCpuInfo->cpuTime, vCpuInfo->cpu);
	} 

   
   
   return 0;
}