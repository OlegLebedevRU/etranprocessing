USE [Service]
GO
/****** Object:  StoredProcedure [dbo].[licensebilling_check]    Script Date: 29.07.2026 21:29:34 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
ALTER PROCEDURE  [dbo].[licensebilling_check]
@serialNumber int
 AS

--declare @serialNumber int
--set @serialNumber  = 8426

declare @org_id int
declare @kiosk_id int
declare @ps_id int

declare @balance  bigint 
set @balance  =0

declare @IsOk tinyint 
set @IsOk = 1 -- some error

select top 1 @org_id = org_id, @kiosk_id = kiosk_id from Certificates where serial_number = @serialNumber

declare @ldt datetime
select @ldt = license from Kiosks where kiosk_id = @kiosk_id 

if @kiosk_id > 0 begin
select @ps_id = ps_id , @balance = amount from PayProperties where org_id  = @org_id and ps_id = 10
if @ps_id  > 0
set @IsOk =0
end

if @ldt < getdate()
set @IsOk = 2

select @balance balance, @IsOk 'state'

--end

