{*
Delphi example code:
*}

..........

procedure TfrmMain.Button1Click(Sender: TObject);
var
	res : integer;
	pkey: IPRIV_KEY;
	S   : string;
begin
	Crypt_Initialize;
	res:= Crypt_OpenSecretKeyFromFile(IPRIV_ENGINE_RSAREF, 'secret.key', '1111111111', @pkey);
	if res=0 then
	begin
		SetLength(S, 1000);
		res:=Crypt_Sign( PChar('test'), -1, PChar(S), 1000, @pkey);
		if res=0 then
		begin
			memo1.Text := S;
		end;
		Crypt_CloseKey(@pkey);

		res:=Crypt_OpenPublicKeyFromFile(IPRIV_ENGINE_RSAREF, 'pubkeys.key', 17033, @pkey, nil);
		if res=0 then
		begin
			res:=Crypt_Verify(PChar(S), -1, nil, 0, @pkey);
			Crypt_CloseKey(@pkey);
		end;
	end;
	Crypt_Done;
end; 


..........