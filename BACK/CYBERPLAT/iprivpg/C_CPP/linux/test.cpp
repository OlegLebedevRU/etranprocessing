#include <stdio.h>
#include "libipriv.h"
#ifdef _WIN32
#include <fcntl.h>
#include <sys\stat.h>
#include <io.h>
#include <process.h>
#endif


int eng=IPRIV_ENGINE_RSAREF;


int sign_and_verify_512(void)
{
	int rc;
	IPRIV_KEY sec;
	IPRIV_KEY pub1;
	IPRIV_KEY pub2;
	char temp[1024];
	FILE* fp;

	rc=Crypt_OpenSecretKeyFromFile(eng,"secret.key","1111111111",&sec);			// Загрузка собственного закрытого ключа
	if(!rc)
	{
		rc=Crypt_OpenPublicKeyFromFile(eng,"pubkeys.key",17033,&pub1,0);		// Загрузка собственного открытого ключа
		if(!rc)
		{
			rc=Crypt_OpenPublicKeyFromFile(eng,"pubkeys.key",17033,&pub2,&pub1);	// Загрузка собственного открытого ключа с проверкой подписи (для примера)
			if(!rc)
			{
				
				rc=Crypt_Sign("Hello world",-1,temp,sizeof(temp),&sec);
				if(rc>0)
				{
					printf("%s\n",temp);

					rc=Crypt_Verify(temp,rc,0,0,&pub2);
					printf("VERIFY: %i\n",rc);

				}else if(!rc)
					rc=-1000;

				Crypt_CloseKey(&pub2);
			}
			Crypt_CloseKey(&pub1);
		}
		Crypt_CloseKey(&sec);
	}
	return rc;
}

int main(void)
{
	IPRIV_KEY sec;
	IPRIV_KEY pub1;
	IPRIV_KEY pub2;

#ifdef _WIN32
	setmode(1,O_BINARY);
	setmode(2,O_BINARY);
#endif
	Crypt_Initialize();
	
	sign_and_verify_512();
	
	Crypt_Done();


	return 0;
}

