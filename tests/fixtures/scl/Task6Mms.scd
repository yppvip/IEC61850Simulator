<?xml version="1.0" encoding="UTF-8"?>
<!-- Self-authored contract fixture. Not XSD validated. No engineering content. -->
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4">
  <Header id="Task6MmsSynthetic"/>
  <Communication><SubNetwork name="DemoBus" type="8-MMS">
    <ConnectedAP iedName="Publisher" apName="Model"><Address><P type="IP">192.0.2.10</P><P type="OSI-TSEL">0001</P></Address></ConnectedAP>
    <ConnectedAP iedName="Publisher" apName="Alias"><Address><P type="IP">192.0.2.11</P></Address></ConnectedAP>
  </SubNetwork></Communication>
  <IED name="Publisher">
    <AccessPoint name="Model"><Server><LDevice inst="LD0" ldName="DemoDomain">
      <LN0 lnClass="LLN0" inst="" lnType="Zero">
        <DataSet name="Events">
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" fc="ST"/>
        </DataSet>
        <ReportControl name="brStatus" rptID="CustomReport01" buffered="true" indexed="true" datSet="Events" confRev="1" bufTime="20" intgPd="1000">
          <TrgOps dchg="true" qchg="true" dupd="false" period="true" gi="true"/>
          <OptFields seqNum="true" timeStamp="true" dataSet="true" reasonCode="true"/>
          <RptEnabled max="2">
            <ClientLN iedName="SinkZ" apRef="Client" ldInst="C" lnClass="GGIO" lnInst="1"/>
            <ClientLN iedName="SinkA" apRef="Client" ldInst="C" lnClass="GGIO" lnInst="1"/>
          </RptEnabled>
        </ReportControl>
        <ReportControl name="urStatus" buffered="false" indexed="false" datSet="Events" confRev="1">
          <TrgOps dchg="true"/><OptFields seqNum="true"/><RptEnabled max="1"/>
        </ReportControl>
        <ReportControl name="defaults" datSet="Events" confRev="1">
          <TrgOps/><OptFields/>
        </ReportControl>
        <ReportControl name="emptyId" rptID="" buffered="true" indexed="true" datSet="Events" confRev="1">
          <TrgOps/><OptFields/><RptEnabled/>
        </ReportControl>
      </LN0>
      <LN lnClass="GGIO" inst="1" lnType="Signals"/>
    </LDevice></Server></AccessPoint>
    <AccessPoint name="Alias"><ServerAt apName="Model"/></AccessPoint>
  </IED>
  <IED name="SinkZ"><AccessPoint name="Client"><Server><LDevice inst="C"><LN0 lnClass="LLN0" inst="" lnType="Zero"/><LN lnClass="GGIO" inst="1" lnType="Signals"/></LDevice></Server></AccessPoint></IED>
  <IED name="SinkA"><AccessPoint name="Client"><Server><LDevice inst="C"><LN0 lnClass="LLN0" inst="" lnType="Zero"/><LN lnClass="GGIO" inst="1" lnType="Signals"/></LDevice></Server></AccessPoint></IED>
  <DataTypeTemplates>
    <LNodeType id="Zero" lnClass="LLN0"/>
    <LNodeType id="Signals" lnClass="GGIO"><DO name="Ind1" type="Status"/><DO name="Unlisted" type="Status"/></LNodeType>
    <DOType id="Status" cdc="SPS"><DA name="stVal" bType="BOOLEAN" fc="ST" dchg="true"/><DA name="q" bType="Quality" fc="ST" qchg="true"/></DOType>
  </DataTypeTemplates>
</SCL>
