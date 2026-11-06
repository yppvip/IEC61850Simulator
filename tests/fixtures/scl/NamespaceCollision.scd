<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" xmlns:vendor="urn:synthetic:extension" version="2007" revision="B" release="4">
  <Header id="SyntheticBaseline" version="1" revision="0" toolID="IEC61850Simulator-Test"/>
  <Communication>
    <SubNetwork name="StationBus" type="8-MMS">
      <ConnectedAP iedName="IED_TEST" apName="AP1">
        <Address><P type="IP">192.0.2.10</P><P type="IP-SUBNET">255.255.255.0</P></Address>
        <GSE ldInst="LD0" cbName="gcb1">
          <Address><P type="MAC-Address">01-0C-CD-01-00-01</P><P type="APPID">0001</P></Address>
          <MinTime unit="s" multiplier="m">4</MinTime><MaxTime unit="s" multiplier="m">1000</MaxTime>
        </GSE>
        <SMV ldInst="LD0" cbName="svcb1">
          <Address><P type="MAC-Address">01-0C-CD-04-00-01</P><P type="APPID">4001</P></Address>
        </SMV>
      </ConnectedAP>
    </SubNetwork>
  </Communication>
  <IED name="IED_TEST" desc="合成测试设备" manufacturer="Synthetic">
    <AccessPoint name="AP1"><Server>
      <Authentication/>
      <LDevice inst="LD0" ldName="EXPLICIT_LD">
        <LN0 lnClass="LLN0" inst="" lnType="T_LLN0">
          <Private type="synthetic"><vendor:IED/><vendor:DataSet/><vendor:ReportControl/></Private>
          <DataSet name="dsEvents" desc="有序成员，含 DO 引用及重复成员">
            <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" fc="ST"/>
            <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
            <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
          </DataSet>
          <ReportControl name="brcbEvents" datSet="dsEvents" rptID="TEST_EVENTS" confRev="1" buffered="true" indexed="true" bufTime="20" intgPd="1000">
            <TrgOps dchg="true" qchg="true" period="true"/>
            <OptFields seqNum="true" timeStamp="true" dataSet="true" reasonCode="true"/>
            <RptEnabled max="2"/>
          </ReportControl>
          <ReportControl name="urcbEvents" datSet="dsEvents" confRev="1" buffered="false" indexed="false">
            <TrgOps dchg="true"/><OptFields seqNum="true"/><RptEnabled max="1"/>
          </ReportControl>
          <GSEControl name="gcb1" type="GOOSE" appID="TEST_GOOSE" datSet="dsEvents" confRev="1"/>
          <SampledValueControl name="svcb1" smvID="TEST_SV" datSet="dsEvents" confRev="1" smpRate="80" nofASDU="1" multicast="true"><SmvOpts refreshTime="true" sampleSynchronized="true" sampleRate="true"/></SampledValueControl>
        </LN0>
        <LN lnClass="GGIO" inst="1" lnType="T_GGIO" desc="合成遥信">
          <DOI name="Ind1"><DAI name="stVal"><Val>true</Val></DAI></DOI>
        </LN>
      </LDevice>
    </Server></AccessPoint>
  </IED>
  <!-- Templates deliberately follow their users (forward references). -->
  <DataTypeTemplates>
    <LNodeType id="T_LLN0" lnClass="LLN0"><DO name="Beh" type="T_ENS"/></LNodeType>
    <LNodeType id="T_GGIO" lnClass="GGIO"><DO name="Ind1" type="T_SPS"/></LNodeType>
    <DOType id="T_ENS" cdc="ENS"><DA name="stVal" bType="Enum" type="T_MODE" fc="ST" dchg="true"/></DOType>
    <DOType id="T_SPS" cdc="SPS"><DA name="stVal" bType="BOOLEAN" fc="ST" dchg="true"/><DA name="q" bType="Quality" fc="ST" qchg="true"/><DA name="t" bType="Timestamp" fc="ST"/></DOType>
    <EnumType id="T_MODE"><EnumVal ord="1">on</EnumVal><EnumVal ord="2">blocked</EnumVal></EnumType>
  </DataTypeTemplates>
</SCL>
