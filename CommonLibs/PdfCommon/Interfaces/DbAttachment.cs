
using System;

namespace PdfCommon.Interfaces
{
    public interface IDbAttachmentParam
    {

        string ConnectionString { get; }

        #region Sql Requests
        string IdRequestName { get; }
        string AllAttachments { get; }
        string GetAttachmentById { get; }
        string AllAttachmentsAllFields { get; }
        #endregion

        #region Fields Name
        string Id { get; }
        string Name { get; }
        string Size { get; }
        string Content { get; }
        #endregion

        
    }

    public interface IDbAttachmentService
    {
        void AddAttachment(Guid id, string fileName, byte[] objData);

        int GetAttachment(Guid attachId, out string fileName, out byte[] objData);


    }
}
