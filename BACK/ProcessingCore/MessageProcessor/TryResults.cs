namespace MessageProcessor
{
    //public enum TryResults
    //{
    //    OK = 1,
    //    PaySystemError = 2,
    //    Exception = 3
    //}

    public enum PaymStates
    {
        start = 1,
        stop = 3,
       blocked = 6
    }

    public enum ResultStatus
    {
        PayOK = 0,
        PayCheckOK = 1,
        Error = -1
    }
}