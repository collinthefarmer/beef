unit ValidateDemo;
var
  Checked, Errors: Integer;

procedure Visit(e: IInterface);
var
  i: Integer;
  problem: string;
begin
  problem := Check(e);
  if problem <> '' then begin
    Inc(Errors);
    AddMessage('DEMO_ERROR ' + FullPath(e) + ': ' + problem);
  end;
  for i := 0 to ElementCount(e) - 1 do
    Visit(ElementByIndex(e, i));
end;

function Process(e: IInterface): Integer;
begin
  Result := 0;
  if GetFileName(GetFile(e)) <> 'BetterEnchantmentEffectsDemo.esp' then Exit;
  Inc(Checked);
  Visit(e);
end;

function Finalize: Integer;
begin
  Result := 0;
  AddMessage('DEMO_CHECK records=' + IntToStr(Checked) + ' errors=' + IntToStr(Errors));
end;
end.
