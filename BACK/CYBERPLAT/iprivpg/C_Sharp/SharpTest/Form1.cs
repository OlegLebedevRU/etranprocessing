using System;
using System.Drawing;
using System.Collections;
using System.ComponentModel;
using System.Windows.Forms;
using System.Data;
using org.CyberPlat;

namespace SharpTest
{
	public class Form1 : System.Windows.Forms.Form
	{
		private System.Windows.Forms.TextBox textBox1;
		private System.Windows.Forms.Button button1;
		private System.Windows.Forms.Button button2;
		private System.Windows.Forms.Button button3;
		private System.Windows.Forms.TextBox textBox2;
		private System.Windows.Forms.FolderBrowserDialog folderBrowserDialog1;
		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.Button button4;
		private System.Windows.Forms.TextBox textBox3;
		private System.Windows.Forms.Label label2;
		private System.Windows.Forms.TextBox textBox4;
		private System.Windows.Forms.Label label3;

		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.Container components = null;

		public Form1()
		{
			//
			// Required for Windows Form Designer support
			//
			InitializeComponent();

			//
			// TODO: Add any constructor code after InitializeComponent call
			//
		}

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		protected override void Dispose( bool disposing )
		{
			if( disposing )
			{
				if (components != null) 
				{
					components.Dispose();
				}
			}
			base.Dispose( disposing );
		}

		#region Windows Form Designer generated code
		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			this.textBox1 = new System.Windows.Forms.TextBox();
			this.button1 = new System.Windows.Forms.Button();
			this.button2 = new System.Windows.Forms.Button();
			this.button3 = new System.Windows.Forms.Button();
			this.textBox2 = new System.Windows.Forms.TextBox();
			this.folderBrowserDialog1 = new System.Windows.Forms.FolderBrowserDialog();
			this.label1 = new System.Windows.Forms.Label();
			this.button4 = new System.Windows.Forms.Button();
			this.textBox3 = new System.Windows.Forms.TextBox();
			this.label2 = new System.Windows.Forms.Label();
			this.textBox4 = new System.Windows.Forms.TextBox();
			this.label3 = new System.Windows.Forms.Label();
			this.SuspendLayout();
			// 
			// textBox1
			// 
			this.textBox1.Location = new System.Drawing.Point(8, 176);
			this.textBox1.Multiline = true;
			this.textBox1.Name = "textBox1";
			this.textBox1.Size = new System.Drawing.Size(368, 192);
			this.textBox1.TabIndex = 0;
			this.textBox1.Text = "Hello World";
			// 
			// button1
			// 
			this.button1.Location = new System.Drawing.Point(304, 32);
			this.button1.Name = "button1";
			this.button1.TabIndex = 1;
			this.button1.Text = "Подписать";
			this.button1.Click += new System.EventHandler(this.button1_Click);
			// 
			// button2
			// 
			this.button2.Location = new System.Drawing.Point(304, 64);
			this.button2.Name = "button2";
			this.button2.TabIndex = 2;
			this.button2.Text = "Проверить";
			this.button2.Click += new System.EventHandler(this.button2_Click);
			// 
			// button3
			// 
			this.button3.Location = new System.Drawing.Point(304, 96);
			this.button3.Name = "button3";
			this.button3.TabIndex = 3;
			this.button3.Text = "Очистить";
			this.button3.Click += new System.EventHandler(this.button3_Click);
			// 
			// textBox2
			// 
			this.textBox2.Location = new System.Drawing.Point(8, 32);
			this.textBox2.Name = "textBox2";
			this.textBox2.Size = new System.Drawing.Size(168, 20);
			this.textBox2.TabIndex = 4;
			this.textBox2.Text = "";
			// 
			// label1
			// 
			this.label1.Location = new System.Drawing.Point(8, 8);
			this.label1.Name = "label1";
			this.label1.Size = new System.Drawing.Size(168, 16);
			this.label1.TabIndex = 5;
			this.label1.Text = "Путь к ключам:";
			// 
			// button4
			// 
			this.button4.Location = new System.Drawing.Point(184, 32);
			this.button4.Name = "button4";
			this.button4.Size = new System.Drawing.Size(24, 23);
			this.button4.TabIndex = 6;
			this.button4.Text = "...";
			this.button4.Click += new System.EventHandler(this.button4_Click);
			// 
			// textBox3
			// 
			this.textBox3.Location = new System.Drawing.Point(8, 80);
			this.textBox3.Name = "textBox3";
			this.textBox3.PasswordChar = '*';
			this.textBox3.Size = new System.Drawing.Size(168, 20);
			this.textBox3.TabIndex = 7;
			this.textBox3.Text = "1111111111";
			// 
			// label2
			// 
			this.label2.Location = new System.Drawing.Point(8, 56);
			this.label2.Name = "label2";
			this.label2.Size = new System.Drawing.Size(100, 16);
			this.label2.TabIndex = 8;
			this.label2.Text = "Кодовая фраза:";
			// 
			// textBox4
			// 
			this.textBox4.Location = new System.Drawing.Point(8, 136);
			this.textBox4.Name = "textBox4";
			this.textBox4.Size = new System.Drawing.Size(168, 20);
			this.textBox4.TabIndex = 9;
			this.textBox4.Text = "17033";
			// 
			// label3
			// 
			this.label3.Location = new System.Drawing.Point(8, 112);
			this.label3.Name = "label3";
			this.label3.Size = new System.Drawing.Size(160, 16);
			this.label3.TabIndex = 10;
			this.label3.Text = "Серийный номер кюча:";
			// 
			// Form1
			// 
			this.AutoScaleBaseSize = new System.Drawing.Size(5, 13);
			this.ClientSize = new System.Drawing.Size(392, 381);
			this.Controls.Add(this.label3);
			this.Controls.Add(this.textBox4);
			this.Controls.Add(this.label2);
			this.Controls.Add(this.textBox3);
			this.Controls.Add(this.button4);
			this.Controls.Add(this.label1);
			this.Controls.Add(this.textBox2);
			this.Controls.Add(this.button3);
			this.Controls.Add(this.button2);
			this.Controls.Add(this.button1);
			this.Controls.Add(this.textBox1);
			this.Name = "Form1";
			this.Text = "CyberPlat IPriv test";
			this.Load += new System.EventHandler(this.Form1_Load);
			this.Closed += new System.EventHandler(this.Form1_Closed);
			this.ResumeLayout(false);

		}
		#endregion

		/// <summary>
		/// The main entry point for the application.
		/// </summary>
		[STAThread]
		static void Main() 
		{
			Application.Run(new Form1());
		}

		private void Form1_Load(object sender, System.EventArgs e)
		{
			IPriv.Initialize();
		}

		private void Form1_Closed(object sender, System.EventArgs e)
		{
			IPriv.Done();
		}

		private void button1_Click(object sender, System.EventArgs e)
		{
			IPrivKey sec=null;
			try
			{
				string path="secret.key";
				if(textBox2.Text!="")
					path=textBox2.Text.TrimEnd('\\')+"\\secret.key";

				sec=IPriv.openSecretKey(path,textBox3.Text);
				textBox1.Text=sec.signText(textBox1.Text);
			}
			catch(IPrivException err)
			{
				MessageBox.Show(err.ToString()+" ("+err.code+")");
			}

			if(sec!=null)
				sec.closeKey();
		}

		private void button2_Click(object sender, System.EventArgs e)
		{
			IPrivKey pub=null;
			try
			{
				string path="pubkeys.key";
				if(textBox2.Text!="")
					path=textBox2.Text.TrimEnd('\\')+"\\pubkeys.key";
				pub=IPriv.openPublicKey(path,Convert.ToUInt32(textBox4.Text,10));
				string hhh = pub.verifyText(textBox1.Text);
				MessageBox.Show("Подпись верна");
			}
			catch(IPrivException err)
			{
				MessageBox.Show(err.ToString()+" ("+err.code+")");
			}

			if(pub!=null)
				pub.closeKey();
		}

		private void button3_Click(object sender, System.EventArgs e)
		{
			textBox1.Text="";			
		}

		private void button4_Click(object sender, System.EventArgs e)
		{
			if(folderBrowserDialog1.ShowDialog()==DialogResult.OK)
			{
				textBox2.Text=folderBrowserDialog1.SelectedPath;
			}
		}
	}
}
