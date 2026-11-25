<?xml version="1.0" encoding="UTF-8"?>
<!-- Self-authored source/coverage cases; no engineering or online data. -->
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4">
  <Header id="Task10EvidenceSynthetic"/>
  <IED name="EvidenceIED"><AccessPoint name="AP1"><Server><LDevice inst="LD0" ldName="EvidenceDomain">
    <LN0 lnClass="LLN0" inst="" lnType="Zero">
      <DataSet name="LeafDs">
        <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.mag.f" fc="MX"/>
        <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.mag.f" fc="MX"/>
      </DataSet>
      <DataSet name="ObjectDs"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" fc="MX"/></DataSet>
      <DataSet name="EmptyDaDs"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="" fc="MX"/></DataSet>
      <DataSet name="StructDs"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal" fc="MX"/></DataSet>
      <DataSet name="WithoutUsers"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.ang.f" fc="MX"/></DataSet>
      <DataSet name="ResidualMag"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.res" daName="cVal.mag.f" fc="MX"/></DataSet>
      <DataSet name="GooseDs"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.mag.f" fc="MX"/></DataSet>
      <DataSet name="SvDs"><FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.mag.f" fc="MX"/></DataSet>
      <DataSet name="ArrayDs">
        <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="Array" daName="cVal.ang.f" fc="MX" ix="1"/>
        <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="Array" daName="cVal.ang.f" fc="MX" ix="2"/>
      </DataSet>
      <ReportControl name="ReportA" datSet="LeafDs" buffered="true"/>
      <ReportControl name="ReportB" datSet="LeafDs" buffered="false"/>
      <ReportControl name="ReportObject" datSet="ObjectDs" buffered="true"/>
      <GSEControl name="GooseA" datSet="GooseDs" type="GOOSE"/>
      <SampledValueControl name="SvA" datSet="SvDs" smvID="Synthetic"/>
    </LN0>
    <LN lnClass="MMXU" inst="1" lnType="Measurements">
      <DOI name="PhV"><SDI name="phsA"><SDI name="cVal">
        <SDI name="mag"><DAI name="f"><Val>110</Val></DAI></SDI>
      </SDI></SDI><SDI name="res"><SDI name="cVal"/></SDI></DOI>
      <DOI name="Array" ix="1"><SDI name="cVal"><SDI name="ang"><DAI name="f"/></SDI></SDI></DOI>
      <DOI name="Array" ix="2"><SDI name="cVal"><SDI name="ang"><DAI name="f"/></SDI></SDI></DOI>
    </LN>
    <LN lnClass="MMXN" inst="1" lnType="Magnitude"><DOI name="Vol"><SDI name="mag"><DAI name="f"/></SDI></DOI></LN>
  </LDevice></Server></AccessPoint></IED>
  <IED name="OtherIED"><AccessPoint name="AP1"><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0" inst="" lnType="Zero"/>
    <LN lnClass="MMXU" inst="1" lnType="Measurements"/>
  </LDevice></Server></AccessPoint></IED>
  <DataTypeTemplates>
    <LNodeType id="Zero" lnClass="LLN0"/>
    <LNodeType id="Measurements" lnClass="MMXU">
      <DO name="PhV" type="Wye"/>
      <DO name="NoData" type="Phase"/>
      <DO name="Array" type="Phase" count="3"/>
    </LNodeType>
    <LNodeType id="Magnitude" lnClass="MMXN"><DO name="Vol" type="MagnitudeValue"/></LNodeType>
    <DOType id="Wye" cdc="WYE"><SDO name="phsA" type="Phase"/><SDO name="res" type="Phase"/></DOType>
    <DOType id="Phase" cdc="CMV"><DA name="cVal" fc="MX" bType="Struct" type="Vector"/></DOType>
    <DOType id="MagnitudeValue" cdc="MV"><DA name="mag" fc="MX" bType="Struct" type="FloatValue"/></DOType>
    <DAType id="Vector"><BDA name="mag" bType="Struct" type="FloatValue"/><BDA name="ang" bType="Struct" type="FloatValue"/></DAType>
    <DAType id="FloatValue"><BDA name="f" bType="FLOAT32"/></DAType>
  </DataTypeTemplates>
</SCL>
