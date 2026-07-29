// Decompiled with JetBrains decompiler
// Type: TechGate.InkassMessage
// Assembly: TechGate, Version=1.0.4724.32104, Culture=neutral, PublicKeyToken=null
// MVID: 7D25CA8F-6EB1-4AE3-AFB7-31AA5758287A
// Assembly location: C:\PlaterraSvn\PROCESSING\FRONT\TechGate\bin\TechGate.dll

using System;

namespace TechGate
{
    public class InkassMessage
    {
        private const byte _lenoftype = 17;
        private string _PAYMEXTID;
        private DateTime _INKASSDATA;
        private int _TOTALSUM;
        private int _TOTALCOUNT;
        private int _NOTE0;
        private int _NOTE1;
        private int _NOTE2;
        private int _NOTE3;
        private int _NOTE4;
        private int _NOTE5;
        private int _NOTE6;
        private int _NOTE7;
        private int _NOTE8;
        private int _NOTE9;
        private string _INKASSEXTID;
        private int _SERIAL_NUMBER;
        private int _currency;

        public InkassMessage(params string[] parametr)
        {
            if (17 != parametr.Length)
                throw new Exception("InkassMessage() количество параметров несовпадает");
            for (int index = 0; index < 17; ++index)
            {
                switch (index)
                {
                    case 0:
                        this._PAYMEXTID = Convert.ToString(parametr[index]);
                        break;
                    case 1:
                        this._INKASSDATA = Convert.ToDateTime(parametr[index]);
                        break;
                    case 2:
                        this._TOTALSUM = Convert.ToInt32(parametr[index]);
                        break;
                    case 3:
                        this._TOTALCOUNT = Convert.ToInt32(parametr[index]);
                        break;
                    case 4:
                        this._NOTE0 = Convert.ToInt32(parametr[index]);
                        break;
                    case 5:
                        this._NOTE1 = Convert.ToInt32(parametr[index]);
                        break;
                    case 6:
                        this._NOTE2 = Convert.ToInt32(parametr[index]);
                        break;
                    case 7:
                        this._NOTE3 = Convert.ToInt32(parametr[index]);
                        break;
                    case 8:
                        this._NOTE4 = Convert.ToInt32(parametr[index]);
                        break;
                    case 9:
                        this._NOTE5 = Convert.ToInt32(parametr[index]);
                        break;
                    case 10:
                        this._NOTE6 = Convert.ToInt32(parametr[index]);
                        break;
                    case 11:
                        this._NOTE7 = Convert.ToInt32(parametr[index]);
                        break;
                    case 12:
                        this._NOTE8 = Convert.ToInt32(parametr[index]);
                        break;
                    case 13:
                        this._NOTE9 = Convert.ToInt32(parametr[index]);
                        break;
                    case 14:
                        this._INKASSEXTID = Convert.ToString(parametr[index]);
                        break;
                    case 15:
                        this._SERIAL_NUMBER = Convert.ToInt32(parametr[index]);
                        break;
                    case 16:
                        this._currency = Convert.ToInt32(parametr[index]);
                        break;
                }
            }
        }

        public object[] ToObject()
        {
            object[] objArray = new object[17];
            for (int index = 0; index < 17; ++index)
            {
                switch (index)
                {
                    case 0:
                        objArray[index] = (object)this._PAYMEXTID;
                        break;
                    case 1:
                        objArray[index] = (object)this._INKASSDATA;
                        break;
                    case 2:
                        objArray[index] = (object)this._TOTALSUM;
                        break;
                    case 3:
                        objArray[index] = (object)this._TOTALCOUNT;
                        break;
                    case 4:
                        objArray[index] = (object)this._NOTE0;
                        break;
                    case 5:
                        objArray[index] = (object)this._NOTE1;
                        break;
                    case 6:
                        objArray[index] = (object)this._NOTE2;
                        break;
                    case 7:
                        objArray[index] = (object)this._NOTE3;
                        break;
                    case 8:
                        objArray[index] = (object)this._NOTE4;
                        break;
                    case 9:
                        objArray[index] = (object)this._NOTE5;
                        break;
                    case 10:
                        objArray[index] = (object)this._NOTE6;
                        break;
                    case 11:
                        objArray[index] = (object)this._NOTE7;
                        break;
                    case 12:
                        objArray[index] = (object)this._NOTE8;
                        break;
                    case 13:
                        objArray[index] = (object)this._NOTE9;
                        break;
                    case 14:
                        objArray[index] = (object)this._INKASSEXTID;
                        break;
                    case 15:
                        objArray[index] = (object)this._SERIAL_NUMBER;
                        break;
                    case 16:
                        objArray[index] = (object)this._currency;
                        break;
                }
            }
            return objArray;
        }
    }
}
