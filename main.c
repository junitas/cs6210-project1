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
   virDomainPtr** allDomains = malloc(numOfDomains * sizeof(virDomainPtr));
   int res = virConnectListAllDomains(hypervisor, allDomains, 0);

   virVcpuInfoPtr vCpuInfo = malloc(sizeof(virVcpuInfo));
   unsigned char ** cpuMap = malloc(sizeof(char*));
   int numOfHostCpus = virNodeGetCPUMap(hypervisor, cpuMap, NULL, 0);
   printf("\nFound %i real CPUs on host.\n", numOfHostCpus);


   int res2 = virDomainGetVcpus(**allDomains, vCpuInfo, 3, *cpuMap, sizeof(*cpuMap));
   printf("\nReturn value from virDomainGetVcpus: %i\n", res2);

   printf("\nvCPU Number: %i, vCpu State: %i, vCpu CPU Time (ns): %llu, pCpu Number: %i\n", vCpuInfo->number, vCpuInfo->state, vCpuInfo->cpuTime, vCpuInfo->cpu);


   
   
   return 0;
}