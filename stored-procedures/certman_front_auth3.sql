USE [Service]
GO
/****** Object:  StoredProcedure [dbo].[certman_front_auth3]    Script Date: 29.07.2026 17:44:56 ******/
SET ANSI_NULLS ON
GO
SET QUOTED_IDENTIFIER ON
GO
ALTER PROCEDURE [dbo].[certman_front_auth3]
@pin 		varchar(50) 
AS

--declare @pin 		varchar(50) 
--set @pin  ='demo'

declare @cert_id_local	 int
declare @status_id_local int

declare @error	int
set @error=0

declare @termtypeid int
declare @termtype varchar(50)
declare @termnumber varchar(50)
set @termtype = 'terminal@e-transfer.ru'

if @pin = 'demo' or @pin = 'demo00'
begin

	-- можно 
	declare @a table (kiosk_id int)
	-- запрещено
	declare @b table (kiosk_id int)
	declare @kiosk_id  int
	insert into @a
	select kiosk_id from kiosks 
	where number>= 9900 and number <= 9999 

	insert into @b
	select kiosk_id  from Certificates where kiosk_id in (select kiosk_id from @a ) 
	and 
	( status_id = 2 or (status_id = 1 and update_datetime > DATEADD(mi, -3, getdate()) ) )

	
	select top 1 @kiosk_id = kiosk_id  from Certificates where 
	kiosk_id in (select kiosk_id from @a )
	and kiosk_id not in (select kiosk_id from @b )

	-- если все сертификаты заняты, то смотрим есть ли свободные терминалы
	if @kiosk_id is null 
	select top 1 @kiosk_id = kiosk_id from Kiosks where kiosk_id in (select kiosk_id from @a ) 
	and kiosk_id not in (select kiosk_id from @b ) order by kiosk_id

	declare @number int
	set @number =(select number from kiosks where kiosk_id = @kiosk_id)

	--print '1 @number ' + cast( isnull(@number,0) as varchar(20) )


--  отзыв пока только вручную
--	if @kiosk_id is null
--	set @kiosk_id = 
--	(
--	        select top 1 kiosk_id from Certificates where kiosk_id in (select kiosk_id from @a ) and status_id = 2 order by update_datetime
--	)

	update Certificates set status_id = 3 where  kiosk_id = @kiosk_id  
	select @number = number from kiosks where kiosk_id  = @kiosk_id  

	--print '2 @number ' + cast( isnull(@number,0) as varchar(20) )

	if @number = null begin
		select 'error' 'result', 'Нет свободных терминалов.' 'descr', 5 'code'
	goto end_sp
	end
	exec GeneratePin @pin output
--	print 'AAAAAAAAAAAAAAA' print @number print @pin	
	
	exec certman_new @number, @pin 

end



	select 
	@cert_id_local = cert_id, 
	@status_id_local = [status_id],
	@termtypeid  = model_id,
	@termnumber = cast(number as varchar(20))
             from service..Certificates cer 
	inner join service..Kiosks ki on(cer.kiosk_id = ki.kiosk_id)
	where pin= @pin

	if isnull(@cert_id_local,0) =0 begin select 'error' 'result', 'пин-код не существует' 'descr' , 2 'code' goto end_sp end 
 	if @status_id_local  != 1 begin select 'error' 'result', 'сертификат установлен или отозван' 'descr', 3 'code'  goto end_sp end

--	if exists (select *   from service..Certificates cer 
--	inner join service..Kiosks ki on(cer.kiosk_id = ki.kiosk_id)
--	where number = @termnumber and cer.status_id = 2)	begin
--	select 'error' 'result', 'Установка невозможна: терминал уже имеет действующий сертификат!' 'descr' goto end_sp	
--	end

--if @termtypeid = 5
--set @termtype = 'web@e-transfer.ru'

select 
'ok' 'result',  'SubCA' 'catype' ,  'Microsoft Enhanced Cryptographic Provider v1.0' 'prov',
'https://etranprocessing.ru/XEnrollService/XEnrollService.ashx' 'url', 
'CN='+cer.common_name+',O='+ cast(cer.org_id as varchar(20))+',OU='+@termnumber+',S=msk,C=ru,L=moscow'+',E='+@termtype 'dn', @pin 'pin', 0 'code' from service..Certificates cer
where cer.cert_id = @cert_id_local
end_sp:
