#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_OF_TESTS 3
#define NUM_OF_PAGES_TO_ALLOCATE 10
#define PGSIZE 4096
 //Task 1: Protecting Pages
void Protecting_Pages_pmalloc_test(void);
void Protecting_Pages_protect_page_test(void);
void Protecting_Pages_pfree_test(void);
void failed_test(char* error);
void allocate_pages(char** pages);
void check_pages_aligned(char** pages);
void free_pages(char** pages);
void protect_pages(char** pages);
void protecting_pages_test();
//Task 2-4: Page replacement schemes & Paging framework & Enhanced process details viewer
void replacement_schemes_test();
void Replacement_Schemes_LIFO_test();
void Replacement_Schemes_SCFIFO_test();
void Replacement_Schemes_NONE_test();
char *echoargv[] = { "echo", "NONE", "TEST", "PASSED", 0 };


int main(void) 
{
    printf(1,"**************************STARTING MyMemTest!**********************\n\n");
    char input[10];
    printf(1, "Press enter for:\n   &Page replacement schemes\n   &Paging framework\n   &Enhanced process details viewer tests.\n");
	gets(input, 10);
    //Task 2-4: Page replacement schemes & Paging framework & Enhanced process details viewer
    replacement_schemes_test();
    printf(1, "Press enter for protecting_pages_test.\n");
	gets(input, 10);
    // Task 1: Protecting Pages
    protecting_pages_test();
    printf(1,"**************************END OF MyMemTest!**********************\n\n");
    exit();
}

void
replacement_schemes_test()
{
    printf(1,"*****start Page replacement schemes and Paging framework test!*****\n\n");
    //Task 3: Page replacement schemes
        #ifdef LIFO
            printf(1,"*****start LIFO_test!*****\n\n");
            Replacement_Schemes_LIFO_test();
            printf(1,"*****end LIFO_test!*****\n\n");

        #endif

        #ifdef SCFIFO
            printf(1,"*****start SCFIFO_test!*****\n\n");
            Replacement_Schemes_SCFIFO_test();
            printf(1,"*****end SCFIFO_test!*****\n\n");
        #endif

        #ifdef NONE
            printf(1,"*****start NONE_test!*****\n\n");
            Replacement_Schemes_NONE_test(),
            printf(1,"*****end NONE_test!*****\n\n");
        #endif
}

//Task 1: Protecting Pages
void 
protecting_pages_test()
{
    char input[10];
    printf(1,"*****start protecting_pages test!*****\n\n");

    void (*protecting_pages_tests[NUM_OF_TESTS])() = {
            //Task 1: Protecting Pages
            Protecting_Pages_pmalloc_test,
            Protecting_Pages_protect_page_test,
            Protecting_Pages_pfree_test,
        };
        int i = 0;
        for ( ; i < NUM_OF_TESTS ; i++){
            if (i!=0){
                printf(1, "Press enter for next_protecting_pages_test.\n");
	            gets(input, 10);
            }
            protecting_pages_tests[i]();
        }
}
    
void //PMALLOC
Protecting_Pages_pmalloc_test()
{
    // char input[10];
    printf(1,"start PMALLOC_test!\n\n");
    char* pages[NUM_OF_PAGES_TO_ALLOCATE];
    allocate_pages(pages);
    check_pages_aligned(pages);         
    //for ctrl+P
    // printf(1,"\npress ctrl+P for enhanced process details, else press enter \n");
    // gets(input, 10);
    //check aligned
    free_pages(pages);
    printf(1,"\ntest ended!\n\n");
}
void //PROTECT
Protecting_Pages_protect_page_test()
{
    // char input[10];
    printf(1,"start PROTECT_PAGE_test!\n\n");
    char* pages[NUM_OF_PAGES_TO_ALLOCATE];
    allocate_pages(pages);   
    //protect
    protect_pages(pages);
    // printf(1,"\npress ctrl+P for enhanced process details, else press enter \n");
    // gets(input, 10);
    //check protect
    // check_protected_pages(pages);
    free_pages(pages);

    printf(1,"\ntest ended!\n\n");
    
}
void //PFREE
Protecting_Pages_pfree_test()
{
    // char input[10];
    int pfree_result;

    printf(1,"START PFREE_test TEST!\n\n");
    char* pages[NUM_OF_PAGES_TO_ALLOCATE];
    allocate_pages(pages);   
    //check protect
    // check_protected_pages(pages);
    printf(1,"  free_pages\n");
    for (int i = 0; i < NUM_OF_PAGES_TO_ALLOCATE ; i++){
        if ( i % 2 != 0 ){
            if (pfree(pages[i]) > 0)
             goto bad;
            else 
                free(pages[i]);
        }
        else if ((pfree_result = pfree(pages[i])) < 0 ){
            printf(1,"pfree returned result: %x \n", pfree_result);
            goto bad;
        }
    }
    // printf(1,"\npress ctrl+P for enhanced process details, else press enter \n");
    // gets(input, 10);
    printf(1,"\ntest ended!\n\n");

    return ;
    bad:
        failed_test("PMALLOC - pfree_test");
}

void fill_physical_memory(char **arr)
{
    //already has 4 pages from parent - shell
	// Allocate all remaining 12 physical pages
	for (int i = 0; i < 12; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "all physical pages taken.\nPress ctrl+P for enhanced process details, else enter.\n");
}

//Task 2-3: Page replacement schemes and Paging framework
void 
Replacement_Schemes_LIFO_test()
{
	int i, j;
	char *arr[14];
	char input[10];
    fill_physical_memory(arr);
   	gets(input, 10);
	arr[12] = sbrk(PGSIZE);
	printf(1, "arr[12]=0x%x\n", arr[12]);
	printf(1, "Called sbrk(PGSIZE).one page in swap file.\nPress ctrl+P for enhanced process details, else enter.\n");
	gets(input, 10);
	//This would cause page 2 to move to the swap file
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[13]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE).two pages in swap file.\nPPress ctrl+P for enhanced process details, else enter.\n");
	gets(input, 10);
	//Access page 3, causing a PGFLT, since it is in the swap file. 
	for (i = 12; i < 14; i++) {
		for (j = 0; j < PGSIZE; j++)
			arr[i][j] = 'a';
	}
	printf(1, "2 page faults should have occurred.\nPress ctrl+P for enhanced process details, else enter.\n");
	gets(input, 10);
	if (fork() == 0) {
		printf(1, "Child code is running.\n");
		printf(1, " \nPress ctrl+P for enhanced process details for pid %d, else enter.\n", getpid());
		gets(input, 10);
		arr[11][0] = 'b';
		printf(1, "A page fault should have occurred.\nPress ctrl+P for enhanced process details, else enter.\n");
		gets(input, 10);
        printf(1, "exit the child code.\n");
		exit();
	}
	else {
		wait();
		sbrk(-14 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress ctrl+P for enhanced process details, else enter.\n");
		gets(input, 10);
        printf(1, "exit father code.\n");
    }
}
void 
Replacement_Schemes_SCFIFO_test()
{
    int i, j;
	char *arr[14];
	char input[10];
    fill_physical_memory(arr);

	gets(input, 10);
	//page 1 will be swapped out.
	arr[12] = sbrk(PGSIZE);
	printf(1, "arr[12]=0x%x\n", arr[12]);
	printf(1, "Called sbrk(PGSIZE) for the 13th time, no page fault should occur and one page in swap file.\nPress ctrl+P for enhanced process details, else enter.\n");
	gets(input, 10);
	//page 3 will be swapped out. pages 1 and 3 are in swapfile.
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[13]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE) for the 14th time, no page fault should occur and two pages in swap file.\nPress ctrl+P for enhanced process details, else enter.\n");
	gets(input, 10);
	//Access page 3, causing a PGFLT. It would swapped with page 4. 
    //Page 4 is accessed next, so another PGFLT is invoked and this process repeats a total of 5 times.
	for (i = 0; i < 5; i++) {
		for (j = 0; j < PGSIZE; j++)
			arr[i][j] = 'k';
	}
	printf(1, "5 page faults should have occurred.\nPress ctrl+P for enhanced process details, else enter.\n");
	gets(input, 10);
	if (fork() == 0) {
		printf(1, "Child code running.\n");
		printf(1, " \nPress ctrl+P for enhanced process details for pid %d, else enter.\n", getpid());
		gets(input, 10);
		arr[5][0] = 'k';
		printf(1, "A Page fault should have occurred in child proccess.\nPress ctrl+P for enhanced process details, else enter.\n");
		gets(input, 10);
        printf(1, "exit child code.\n");
        printf(1, "Press enter to return to parent code .\n");
		gets(input, 10);
		exit();
	}
	else {
		wait();
		sbrk(-14 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress ctrl+P for enhanced process details, else enter.\n");
		gets(input, 10);
        printf(1, "exit father code.\n");
	}
	
}
void 
Replacement_Schemes_NONE_test()
{
    char *arr[200];

    // Allocate all remaining 12 physical pages
	for (int i = 0; i < 200; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "allocated 200 pages.\n");

  if (fork() == 0) {
		printf(1, "Child code running.\n");
		printf(1, "exec test\n");
        if(exec("echo", echoargv) < 0){
            printf(1, "exec echo failed\n");
        }
        printf(1, "exit child code.\n");
		exit();
	}
	else {
		wait();
        sbrk(-200 * PGSIZE);
        printf(1, "Deallocated all 200 pages.\nPress enter.\n");
        printf(1, "exit father code.\n");
	}
}

void 
failed_test(char* error)
{
    printf(1,"test failed  **** %s ERROR!****\n", error);
}
void 
allocate_pages(char** pages)
{
    printf(1,"  allocating PROTECTED and NOT PROTECTED pages with pmalloc and malloc\n");
    int i;
    for ( i = 0; i < NUM_OF_PAGES_TO_ALLOCATE ; i++){
        if ((i%2) != 0){
            if ( (pages[i] = (char*)malloc(4000))== 0){
               printf(1, "malloc returned 0.. pmalloc test FAILED\n");
               goto bad;
            }
        }
        else if ( (pages[i] = (char*)pmalloc()) == 0){
            printf(1, "pmalloc returned 0.. pmalloc test FAILED\n");
            goto bad;
        }
    }
    
    return ;
    bad:
        failed_test("PMALLOC - allocate pages");

}
void
check_pages_aligned(char** pages)
{
    int i;
    uint result;
    printf(1,"  check_pages_aligned\n");
    for ( i = 0; i < NUM_OF_PAGES_TO_ALLOCATE ; i++){
        if ( i % 2 == 0 && ((result = (uint)pages[i])%PGSIZE) != 0 ){
            printf(1,"pmalloc returned address: %x \n", result);
            goto bad;
        }
    }
    return ;
    bad:
        failed_test("PMALLOC - check_pages_aligned");

}
void
free_pages(char** pages)
{
    int i;
    int pfree_result;

    printf(1,"  free_pages\n");
    for ( i = 0; i < NUM_OF_PAGES_TO_ALLOCATE ; i++){
        if ( i % 2 != 0 )
            free(pages[i]);
        else if ((pfree_result = pfree(pages[i])) < 0 ){
            printf(1,"pfree returned result: %x \n", pfree_result);
            goto bad;
        }

    }
    return ;
    bad:
        failed_test("PMALLOC - free_pages");
}
void 
protect_pages(char** pages)
{
    int i;

    printf(1,"  protect_pages\n");
    for ( i = 0; i < NUM_OF_PAGES_TO_ALLOCATE ; i++){
        if ( (i % 2) != 0 ){
            if ((protect_page(pages[i])) > 0 ){
                goto bad;
            }
        }
        else{
            if ((protect_page(pages[i])) < 0 ){
                goto bad;
            }
        }
    }
    return ;
    bad:
        failed_test("PMALLOC - protect_pages");
}