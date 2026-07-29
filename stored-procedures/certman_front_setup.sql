USE [Service]
GO
/****** Object:  StoredProcedure [dbo].[certman_front_setup]    Script Date: 29.07.2026 21:26:10 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
ALTER PROCEDURE [dbo].[certman_front_setup]
@pin 			varchar(50),
@serial_number 		int,
@cpserial 		varchar(50),
@ca_serial_number 	char(32)
as


declare @cert_id_local	int
declare @org_id	int
declare @status_id_local	int
declare @kiosk_id	int
declare @number	int
declare @ca_number	int

declare @error	int
set @error=0

BEGIN TRANSACTION 

select @ca_number = ca_number from CA where ca_serial_number = @ca_serial_number
if @ca_number > 0
begin
	select
	@org_id = org_id,
	@cert_id_local = cert_id, 
	@status_id_local = [status_id],
	@kiosk_id = kiosk_id
	from Certificates 	where pin= @pin

	if isnull(@cert_id_local,0) =0 begin select 'error' 'result', 'Такого пина нет' 'descr', 2 'code' goto end_sp end 
 	if @status_id_local  != 1  begin select 'error' 'result', 'Ошибка состояния серт-та' 'descr', 4 'code'  goto end_sp end

	select @number = number from Kiosks where kiosk_id = @kiosk_id
	
	--if @number != 5
        update service..Certificates set status_id = 3 where kiosk_id = @kiosk_id and status_id = 2 

	select @error = @@error if @error !=0  begin select 'error' 'result', cast(@error as varchar(20)) 'descr' goto end_sp end

	update	Certificates  set serial_number= @serial_number, cpserial  = @cpserial, status_id = 2, 
	update_datetime= getdate(), ca_number = @ca_number   where cert_id = @cert_id_local
	select @error = @@error if @error !=0  begin select 'error' 'result', cast(@error as varchar(20)) 'descr' goto end_sp end

	exec SMS_PostEvent @org_id, @number, 2

	update Kiosks set cert_valid = 0 where kiosk_id = @kiosk_id
	select @error = @@error if @error !=0  begin select 'error' 'result', cast(@error as varchar(20)) 'descr' goto end_sp end

	EXEC PRTL_WS_DelAllScriptsForKiosk @kiosk_id
	select 'ok' 'result',  0 'code' 
end 
else 
begin
	select 'error' 'result', 'CA не идентифицирован' 'descr', 6 'code' goto end_sp
end

end_sp:
if @error != 0
ROLLBACK 
ELSE
COMMIT
