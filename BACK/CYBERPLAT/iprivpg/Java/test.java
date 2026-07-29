/*
   CopyRight (C) 1998-2005 CyberPlat.Com. All Rights Reserved.
   e-mail: support@cyberplat.com
*/

import org.CyberPlat.*;

class test
{
	public static void main(String args[])
	{
		IPriv.setCodePage("");	//"" - for system default,"WINDOWS-1251","UTF8","KOI8-R","CP866"
		if(args.length<1)
		{
			System.err.println("USAGE: test message");
			System.exit(0);
		}

		IPrivKey sec=null;
		IPrivKey pub=null;
		try
		{
			// Загрузка закрытого ключа
			sec=IPriv.openSecretKey("secret.key","1111111111");
			// Загрузка открытого ключа
			pub=IPriv.openPublicKey("pubkeys.key",17033);
/*
			IPriv.setPINCodePKCS11("1111111111");
			// Загрузка закрытого ключа
			sec=IPriv.openSecretKeyPKCS11(17033);
			// Загрузка открытого ключа
			pub=IPriv.openPublicKeyPKCS11(17033);
*/

			// Формирование подписи закрытым ключом
			String msg=sec.signText(args[0]);
			System.out.println(msg);
			
			// Проверка собственной подписи своим открытым ключом (исключение если ошибка)
			msg=pub.verifyText(msg);
			System.out.println(msg);

			// Шифрование открытым ключом получателя
			msg=pub.encryptText("Hello world");
			System.out.println(msg);

			// Дешифрование закрытым ключом получателя
			msg=sec.decryptText(msg);
			System.out.println(msg);
		}
		catch(IPrivException err)
		{
			System.out.println(err.toString()+" ("+err.code+")");
		}

		// Освобождение ресурсов (обязательно вызывать явно)
		if(sec!=null)
			sec.closeKey();
		if(pub!=null)
			pub.closeKey();
	}
};
