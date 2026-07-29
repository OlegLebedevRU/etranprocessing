<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:msxsl="urn:schemas-microsoft-com:xslt" exclude-result-prefixes="msxsl">
  <xsl:param name="picturePath"/>
  <xsl:output method="xml" indent="yes"/>
  
  <xsl:template match="/Profile">
    <itext>
      <paragraph align="Center">
        <phrase fontstyle="bold" size="16.0" >Информация о пользователе</phrase>
      </paragraph>

      <image >
        <xsl:attribute name="url">
          <xsl:value-of select="$picturePath"/>
        </xsl:attribute>
      </image>
        
      <table width="100%" columns="3" cellpadding="1" cellspacing="1" borderwidth="0.5" red="0" 
             green="0" blue="0" left="true" right="true" top="true" bottom="true" widths="33;33;33">
        <row >
          <cell borderwidth="0.5" red="0" green="0" blue="0" left="false" 
                right="true" top="false" bottom="true" header="true" horizontalalign="Center">
            <phrase size="10.0" >Фамилия</phrase>
            </cell>
          <cell borderwidth="0.5" red="0" green="0" blue="0" left="false" 
                right="true" top="false" bottom="true" header="true" horizontalalign="Center">
            <phrase  size="10.0" >Имя</phrase>
          </cell>
          <cell borderwidth="0.5" red="0" green="0" blue="0" left="false" 
                right="false" top="false" bottom="true" header="true" horizontalalign="Center">
            <phrase size="10.0" >Отчество</phrase>
          </cell>
        </row>
        <row >
          <cell borderwidth="0.5" red="0" green="0" blue="0" left="false" 
                right="true" top="false" bottom="false" header="false" horizontalalign="Left">
            <phrase size="10.0" >
              <xsl:value-of select="FirstName"/>
            </phrase>
          </cell>
          <cell borderwidth="0.5" red="0" green="0" blue="0" left="false" 
                right="true" top="false" bottom="false" header="false" horizontalalign="Left">
            <phrase size="10.0" >
              <xsl:value-of select="SecondName"/>
            </phrase>
          </cell>
          <cell borderwidth="0.5" red="0" green="0" blue="0" left="false" 
                right="false" top="false" bottom="false" header="false" horizontalalign="Left">
            <phrase size="10.0" >
              <xsl:value-of select="LastName"/>
            </phrase>
          </cell>
        </row>
      </table>

      <xsl:apply-templates select="Subtexts"/>
              
    </itext>
  </xsl:template>

  <xsl:template match="Subtexts">
    <paragraph align="Justify">
      <phrase size="14.0" >
        <xsl:value-of select="Text"/>
      </phrase>
    </paragraph>
    <newline/>
  </xsl:template>
  
</xsl:stylesheet>
