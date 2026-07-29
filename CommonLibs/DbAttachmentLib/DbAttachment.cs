using System;
using PdfCommon.Interfaces;
using System.Data;
using System.Data.SqlClient;

namespace DbAttachmentLib
{
    public class DbAttachment : IDbAttachmentService
    {
        readonly SqlConnection _objConn = new SqlConnection();
        private readonly IDbAttachmentParam _dbAttachmentParam;

        public DbAttachment(IDbAttachmentParam dbAttachmentParam)
        {
            _dbAttachmentParam = dbAttachmentParam;
            _objConn.ConnectionString = _dbAttachmentParam.ConnectionString;
        }

        public void AddAttachment(Guid id, string fileName, byte[] objData)
        {
            var objAdapter = new SqlDataAdapter(_dbAttachmentParam.AllAttachmentsAllFields, _objConn)
            {
                MissingSchemaAction = MissingSchemaAction.AddWithKey
            };
            using (new SqlCommandBuilder(objAdapter))
            {
                var objTable = new DataTable();
                objAdapter.Fill(objTable);
                var objRow = objTable.NewRow();
                objRow[_dbAttachmentParam.Id] = id;
                objRow[_dbAttachmentParam.Name] = fileName;
                objRow[_dbAttachmentParam.Size] = objData.Length;
                objRow[_dbAttachmentParam.Content] = objData;  //our file
                objTable.Rows.Add(objRow); //add our new record
                if (objAdapter.Update(objTable) > 0)
                {
                    //rowId = (int) objTable.Rows[objTable.Rows.Count - 1][_dbAttachmentParam.Id];
                }
                else
                {
                    throw  new Exception("DB Error insert.");
                }
            }
        }

        public int GetAttachment(Guid attachId, out string fileName, out byte[] objData)
        {
            var sqlCmd = new SqlCommand(_dbAttachmentParam.GetAttachmentById, _objConn);
            sqlCmd.Parameters.AddWithValue("@attachId", attachId);
            var objAdapter = new SqlDataAdapter(sqlCmd) {MissingSchemaAction = MissingSchemaAction.AddWithKey};
            using (new SqlCommandBuilder(objAdapter))
            {
                var objTable = new DataTable();
                objAdapter.Fill(objTable);
                var objRow = objTable.Rows[0];
                fileName = (string) objRow[_dbAttachmentParam.Name];
                objData = (byte[]) objRow[_dbAttachmentParam.Content];
                return (int) objRow[_dbAttachmentParam.Size];
            }
        }
    }
}
