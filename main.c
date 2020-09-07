#include <stdio.h>
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
   
   return 0;
}