namespace EtranProcessing
{
    using System.Security.Cryptography.X509Certificates;
    using System.Security.Cryptography;
    using System.Text;
    using System;
    using EtranDispatcher;

    class EtranCrypto
    {

        static public bool VerifyHash(string msg, string signature, string certrek)
        {
            try
            {
                //GlobalObjectsManager.Logger.Info("signature: " + signature);
                X509Certificate2 cert = GetCertFromStore(certrek, X509FindType.FindBySubjectName);
                //return (cert != null) ? true : false;
                return VerifyHash(msg, signature, ref cert);
            }
            catch(Exception e)
            {
                GlobalObjectsManager.Logger.Error(e);
            }
            return false;
        }
        


        static public X509Certificate2 GetCertFromStore(string search, X509FindType type)
        {
            X509Certificate2 cert = null;
            X509Store store = null;
            try
            {
                store = new X509Store(StoreName.My, StoreLocation.CurrentUser);
                store.Open(OpenFlags.ReadOnly);
                X509Certificate2Collection collection = (X509Certificate2Collection)store.Certificates;
                X509Certificate2Collection found = collection.Find(type, search, true);
                if (found.Count == 1)
                {
                    cert = found[0];
                    GlobalObjectsManager.Logger.Info("найден сертификат удовлетвор€ющий условию: " + search);
                }
                else
                    if (found.Count > 1)
                        throw new Exception("найдено больше одного сертификата удовлетвор€ющему условию: " + search);
                    else
                        throw new Exception("не найдено сертификата удовлетвор€ющему условию: " + search);


            }
            catch //(Exception ex)
            {
                //GlobalObjectsManager.Logger.Error("ѕри доступе к пользовательскому сертификату возникло исключение", ex);
                throw;
            }
            finally
            {
                store.Close();
            }
            return cert;
        }


        static public bool VerifyHash(string msg, string signature, ref X509Certificate2 cert)
        {
            RSACryptoServiceProvider rsa_public = cert.PublicKey.Key as RSACryptoServiceProvider;
            return rsa_public.VerifyHash((new SHA1Managed()).ComputeHash(Encoding.Default.GetBytes(msg)), CryptoConfig.MapNameToOID("SHA1"), Convert.FromBase64String(signature));
        }

        static public string HashAndSign(string msg, ref X509Certificate2 cert)
        {
            RSACryptoServiceProvider rsa_private = cert.PrivateKey as RSACryptoServiceProvider;
            return Convert.ToBase64String(rsa_private.SignHash((new SHA1Managed()).ComputeHash(Encoding.Default.GetBytes(msg)), CryptoConfig.MapNameToOID("SHA1")));
        }
    }
}
