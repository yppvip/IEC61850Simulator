<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4">
  <Header id="SyntheticIedBrowsing"/>
  <Communication>
    <SubNetwork name="Bus">
      <ConnectedAP iedName="IED_A" apName="AP1"><Address><P type="IP">192.0.2.1</P></Address></ConnectedAP>
      <ConnectedAP iedName="IED_B" apName="AP1"><Address><P type="IP">192.0.2.2</P></Address></ConnectedAP>
      <ConnectedAP iedName="UNKNOWN" apName="AP1"/>
    </SubNetwork>
  </Communication>
  <IED name="IED_A"><AccessPoint name="AP1"><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0" inst="" lnType="T_LLN0">
      <DataSet name="dsA"><FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/></DataSet>
      <DataSet name="dsA2"/>
      <ReportControl name="ra1" datSet="dsA" rptID="A_FIRST" confRev="1"/>
      <ReportControl name="ra2" datSet="dsA2" rptID="A_SECOND" confRev="1"/>
      <Inputs><ExtRef iedName="IED_B" ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" serviceType="Report" srcLDInst="LD0" srcLNClass="LLN0" srcCBName="rb1"/></Inputs>
    </LN0>
    <LN lnClass="GGIO" inst="1" lnType="T_GGIO"/>
  </LDevice></Server></AccessPoint></IED>
  <IED name="IED_B"><AccessPoint name="AP1"><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0" inst="" lnType="T_LLN0">
      <DataSet name="dsB"><FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/></DataSet>
      <ReportControl name="rb1" datSet="dsB" rptID="B_FIRST" confRev="1"/>
    </LN0>
    <LN lnClass="GGIO" inst="1" lnType="T_GGIO"/>
  </LDevice></Server></AccessPoint></IED>
  <IED name="IED_EMPTY"/>
  <DataTypeTemplates>
    <LNodeType id="T_LLN0" lnClass="LLN0"/>
    <LNodeType id="T_GGIO" lnClass="GGIO"><DO name="Ind1" type="T_SPS"/></LNodeType>
    <DOType id="T_SPS" cdc="SPS"><DA name="stVal" bType="BOOLEAN" fc="ST"/></DOType>
    <LNodeType id="T_UNUSED" lnClass="GGIO"/>
  </DataTypeTemplates>
</SCL>
