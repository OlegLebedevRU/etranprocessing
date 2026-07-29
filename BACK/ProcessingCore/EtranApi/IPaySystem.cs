using System;

namespace Estylesoft.Etran
{
	/// <summary>
	/// »нтерфейс платежной системы.
	/// </summary>
	public interface IPaySystem
	{
        /// <summary>
        /// ѕроизводит запрос платежной системе и возвращает ответ, приобразованный к внутреннему формату системы.
        /// </summary>
        /// <param name="Request">«апрос на проверку или платеж.</param>
        /// <param name="Timeout">ћаксимальное врем€ ожидани€ ответа от платежной системы в миллисекундах.</param>
        /// <returns>ќтвет платежной системы.</returns>
        ResponseMessage DoRequest(RequestMessage Request,int Timeout);
	}
}
