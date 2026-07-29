program K_pay_zhiv;

uses
  Forms,
  k_pay in 'k_pay.pas' {f_LicNum};

{$R *.res}

begin
  Application.Initialize;
  Application.CreateForm(Tf_LicNum, f_LicNum);
  Application.Run;
end.
