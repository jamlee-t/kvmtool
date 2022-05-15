#include <stdio.h>
 
void
__attribute__((constructor)) funcBeforeMain()
{
    printf("%s...\n", __FUNCTION__);
}
 
void
__attribute__((destructor)) funcAfterMain()
{
    printf("%s...\n", __FUNCTION__);
}
 
int main()
{
    printf("main...\n");
    return 0;
}
// ————————————————
// 版权声明：本文为CSDN博主「落尘纷扰」的原创文章，遵循CC 4.0 BY-SA版权协议，转载请附上原文出处链接及本声明。
// 原文链接：https://blog.csdn.net/jasonchen_gbd/article/details/44138877