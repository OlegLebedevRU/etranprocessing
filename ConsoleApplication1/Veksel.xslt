<?xml version="1.0" encoding="utf-8"?>
<fo:root xmlns:fo="http://www.w3.org/1999/XSL/Format">

  <fo:layout-master-set>
    <fo:simple-page-master master-name="simple"
                  page-height="15.7cm"
                  page-width="6.9cm"
                  margin-top="0.1cm"
                  margin-bottom="0.3cm"
                  margin-left="0.3cm"
                  margin-right="0.3cm">
      <fo:region-body margin-top="0.1cm"/>
      <fo:region-before extent="3cm"/>
      <fo:region-after extent="3.5cm"/>
    </fo:simple-page-master>
  </fo:layout-master-set>

  <fo:page-sequence master-reference="simple">

    <fo:static-content flow-name="xsl-region-after" >
      <fo:block padding-top="0.2cm" text-align="center" font-family="Arial">
        <fo:block font-size="9pt">
          <fo:external-graphic src="%param15%"></fo:external-graphic>
        </fo:block>
        <fo:block font-size="7pt" text-align="left">
          Выплачено:
        </fo:block>
        <fo:block font-size="7pt" text-align="left">
        </fo:block>

        <fo:block font-size="6pt" text-align="left">
          %param16%
        </fo:block>
      </fo:block>
    </fo:static-content>

    <fo:flow flow-name="xsl-region-body">

      <fo:block font-size="12pt" color="black" text-align="center" font-family="Arial">
        ПРОСТОЙ ВЕКСЕЛЬ №%param0%
      </fo:block>

      <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
        Дата, место составления векселя: %param1%,
        %param2%
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Предприятие, адрес:
          %param3%,
          %param4%, %param5%
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Обязуется безусловно уплатить по векселю сумму:
          %param6%
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Со дня составления векселя до его предъявления
          к оплате на вексельную сумму начисляются
          проценты по ставке %param7% % годовых,
          которые уплачиваются при оплате векселя.
          Вексельная сумма и проценты должны быть
          выплачены непосредственно лицу:
          %param8%, ПАСП РФ
          № %param9%, адрес: %param10%
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Этот вексель должен быть предъявлен к оплате
          в срок:%param11%
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          и подлежит оплате в момент предъявления.
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Место платежа:%param12%
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Руководитель векселедателя:
          %param13%
        </fo:block>
        <fo:block font-size="8pt" color="black" text-align="left" font-family="Arial">
          Аналог собственноручной подписи
          векселедателя: %param14%
        </fo:block>

      </fo:block>

    </fo:flow>

  </fo:page-sequence>
</fo:root>