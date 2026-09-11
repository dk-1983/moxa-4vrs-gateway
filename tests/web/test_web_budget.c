#include "web/web_budget.h"
#include <assert.h>
#include <stdio.h>
int main(void){web_budget_t b={0};unsigned int d;
 assert(!web_budget_delay(&b,100,0));
 assert(!web_budget_delay(&b,100,10000));
 d=web_budget_delay(&b,100,50000);assert(d==61); /* indivisible 40ms step overshoot */
 assert(web_budget_delay(&b,140,50000)==21);
 assert(!web_budget_delay(&b,161,50000));
 assert(!web_budget_delay(&b,100000,50000)&&b.credit==20000); /* no idle credit hoarding */
 b.wall=0xfffffff0U;b.cpu=0xfffffff0U;b.credit=20000;
 assert(!web_budget_delay(&b,0x10U,0x10U)); /* unsigned wall/CPU wrap */
 (void)web_cpu_us();puts("CPU budget: burst, debt repayment, idle cap, wrap PASS");return 0;
}
