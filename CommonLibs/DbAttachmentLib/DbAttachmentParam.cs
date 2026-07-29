using PdfCommon.Interfaces;

namespace DbAttachmentLib
{
    public class DbAttachmentParam : IDbAttachmentParam
    {
        public virtual string ConnectionString { get
        {
            return @"Data Source=localhost\sqlexpress;Initial Catalog=db2;Integrated Security=True";
        } }
        public string IdRequestName { get { return "@attachId"; } }
        public string AllAttachments { get { return "select [id], [fileName], [fileSize] from [tblAttachments] order by [fileName]"; } }
        public string GetAttachmentById { get { return "select * from [tblAttachments] where [id] = @attachId"; } }
        public string AllAttachmentsAllFields { get { return "select * from [tblAttachments]"; } }
        public string Id { get { return "id"; } }
        public string Name { get { return "fileName"; } }
        public string Size { get { return "fileSize"; }}
        public string Content { get { return "attachment"; }}
    }
}
