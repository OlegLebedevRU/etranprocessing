USE [Organizations]
GO
/****** Object:  Table [dbo].[tb_MedParams]    Script Date: 26.07.2017 15:49:58 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[tb_MedParams](
	[ServiceId] [int] NOT NULL,
	[ParamId] [int] NOT NULL,
 CONSTRAINT [PK_tb_MedParams] PRIMARY KEY CLUSTERED 
(
	[ServiceId] ASC,
	[ParamId] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON) ON [PRIMARY]
) ON [PRIMARY]

GO
/****** Object:  Table [dbo].[tb_MedRyazan]    Script Date: 26.07.2017 15:49:58 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[tb_MedRyazan](
	[Id] [int] NOT NULL,
	[SericeName] [nvarchar](500) NOT NULL,
	[KBK] [nvarchar](50) NOT NULL CONSTRAINT [DF_tb_MedRyazan_KBK]  DEFAULT ((130.)),
	[NDS] [int] NOT NULL CONSTRAINT [DF_tb_MedRyazan_NDS]  DEFAULT ((0)),
 CONSTRAINT [PK_tb_MedRyazan] PRIMARY KEY CLUSTERED 
(
	[Id] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON) ON [PRIMARY]
) ON [PRIMARY]

GO
/****** Object:  Table [dbo].[ts_MedParams]    Script Date: 26.07.2017 15:49:58 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
CREATE TABLE [dbo].[ts_MedParams](
	[Id] [int] NOT NULL,
	[ParamName] [nvarchar](50) NOT NULL,
 CONSTRAINT [PK_ts_MedParams] PRIMARY KEY CLUSTERED 
(
	[Id] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON) ON [PRIMARY]
) ON [PRIMARY]

GO
INSERT [dbo].[tb_MedParams] ([ServiceId], [ParamId]) VALUES (1, 1)
GO
INSERT [dbo].[tb_MedParams] ([ServiceId], [ParamId]) VALUES (1, 2)
GO
INSERT [dbo].[tb_MedParams] ([ServiceId], [ParamId]) VALUES (2, 1)
GO
INSERT [dbo].[tb_MedParams] ([ServiceId], [ParamId]) VALUES (2, 2)
GO
INSERT [dbo].[tb_MedRyazan] ([Id], [SericeName], [KBK], [NDS]) VALUES (1, N'оплата за обучение на ФДПО', N'00000000000000000130', 0)
GO
INSERT [dbo].[tb_MedRyazan] ([Id], [SericeName], [KBK], [NDS]) VALUES (2, N'оплата за обучение в ординатуре', N'00000000000000000130', 0)
GO
INSERT [dbo].[ts_MedParams] ([Id], [ParamName]) VALUES (1, N'учебный год')
GO
INSERT [dbo].[ts_MedParams] ([Id], [ParamName]) VALUES (2, N'Обучающийся')
GO
INSERT [dbo].[ts_MedParams] ([Id], [ParamName]) VALUES (3, N'Код страны')
GO
INSERT [dbo].[ts_MedParams] ([Id], [ParamName]) VALUES (4, N'ФИО')
GO
INSERT [dbo].[ts_MedParams] ([Id], [ParamName]) VALUES (5, N'курс')
GO
INSERT [dbo].[ts_MedParams] ([Id], [ParamName]) VALUES (6, N'факультет')
GO
/****** Object:  StoredProcedure [dbo].[RyazanMed_GetParamsList]    Script Date: 26.07.2017 15:49:58 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
-- =============================================
-- Author:		<Author,,Name>
-- Create date: <Create Date,,>
-- Description:	<Description,,>
-- =============================================
create PROCEDURE [dbo].[RyazanMed_GetParamsList]
AS
BEGIN
	-- SET NOCOUNT ON added to prevent extra result sets from
	-- interfering with SELECT statements.
	SET NOCOUNT ON;

    -- Insert statements for procedure here
	SELECT * from ts_MedParams

END

GO
/****** Object:  StoredProcedure [dbo].[RyazanMed_GetServices]    Script Date: 26.07.2017 15:49:58 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
-- =============================================
-- Author:		<Author,,Name>
-- Create date: <Create Date,,>
-- Description:	<Description,,>
-- =============================================
CREATE PROCEDURE [dbo].[RyazanMed_GetServices]
AS
BEGIN
	-- SET NOCOUNT ON added to prevent extra result sets from
	-- interfering with SELECT statements.
	SET NOCOUNT ON;

    -- Insert statements for procedure here
	SELECT * from tb_MedRyazan

END

GO
/****** Object:  StoredProcedure [dbo].[RyazanMed_GetServicesParams]    Script Date: 26.07.2017 15:49:58 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
-- =============================================
-- Author:		<Author,,Name>
-- Create date: <Create Date,,>
-- Description:	<Description,,>
-- =============================================
CREATE PROCEDURE [dbo].[RyazanMed_GetServicesParams]
AS
BEGIN
	-- SET NOCOUNT ON added to prevent extra result sets from
	-- interfering with SELECT statements.
	SET NOCOUNT ON;

    -- Insert statements for procedure here
	SELECT * from tb_MedParams

END

GO
