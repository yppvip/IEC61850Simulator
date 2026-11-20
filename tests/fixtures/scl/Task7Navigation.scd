<?xml version="1.0" encoding="UTF-8"?>
<!-- Self-authored navigation fixture. Not XSD validated; no engineering content. -->
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4">
  <Header id="Task7NavigationSynthetic"/>
  <IED name="Publisher">
    <AccessPoint name="AP1"><Server>
      <LDevice inst="LD0"><LN0 lnClass="LLN0" inst="" lnType="Zero">
        <DataSet name="Shared" desc="两份报告共用，保留成员出现次数">
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Missing" daName="stVal" fc="ST"/>
        </DataSet>
        <ReportControl name="ReportA" rptID="SyntheticA" buffered="true" indexed="true" datSet="Shared" confRev="1">
          <TrgOps dchg="true"/><OptFields seqNum="true"/>
          <RptEnabled max="1"><ClientLN iedName="Sink" apRef="AP1" ldInst="C" lnClass="GGIO" lnInst="1"/></RptEnabled>
        </ReportControl>
        <ReportControl name="ReportB" rptID="SyntheticB" buffered="false" indexed="false" datSet="Shared" confRev="1">
          <TrgOps dchg="true"/><OptFields seqNum="true"/>
        </ReportControl>
        <GSEControl name="GooseA" datSet="Shared" confRev="1" appID="SyntheticGoose" type="GOOSE"/>
      </LN0><LN lnClass="GGIO" inst="1" lnType="Signals"/></LDevice>
      <LDevice inst="LD1"><LN0 lnClass="LLN0" inst="" lnType="Zero">
        <DataSet name="Shared"><FCDA ldInst="LD1" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/></DataSet>
        <ReportControl name="ReportOtherScope" rptID="SyntheticOther" buffered="false" indexed="false" datSet="Shared" confRev="1">
          <TrgOps dchg="true"/><OptFields seqNum="true"/>
        </ReportControl>
      </LN0><LN lnClass="GGIO" inst="1" lnType="Signals"/></LDevice>
    </Server></AccessPoint>
  </IED>
  <IED name="Sink"><AccessPoint name="AP1"><Server><LDevice inst="C">
    <LN0 lnClass="LLN0" inst="" lnType="Zero"/><LN lnClass="GGIO" inst="1" lnType="Signals"/>
  </LDevice></Server></AccessPoint></IED>
  <DataTypeTemplates>
    <LNodeType id="Zero" lnClass="LLN0"/>
    <LNodeType id="Signals" lnClass="GGIO"><DO name="Ind1" type="Status" desc="合成位置"/><DO name="Unlisted" type="Status"/></LNodeType>
    <DOType id="Status" cdc="SPS"><DA name="stVal" bType="BOOLEAN" fc="ST" dchg="true"/><DA name="q" bType="Quality" fc="ST" qchg="true"/></DOType>
  </DataTypeTemplates>
</SCL>
