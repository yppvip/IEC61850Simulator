<?xml version="1.0" encoding="UTF-8"?>
<s:SCL xmlns:s="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4">
  <s:Header id="SyntheticBaseline" version="1" revision="0" toolID="IEC61850Simulator-Test"/>
  <s:Communication>
    <s:SubNetwork name="StationBus" type="8-MMS">
      <s:ConnectedAP iedName="IED_TEST" apName="AP1">
        <s:Address><s:P type="IP">192.0.2.10</s:P><s:P type="IP-SUBNET">255.255.255.0</s:P></s:Address>
        <s:GSE ldInst="LD0" cbName="gcb1">
          <s:Address><s:P type="MAC-Address">01-0C-CD-01-00-01</s:P><s:P type="APPID">0001</s:P></s:Address>
          <s:MinTime unit="s" multiplier="m">4</s:MinTime><s:MaxTime unit="s" multiplier="m">1000</s:MaxTime>
        </s:GSE>
        <s:SMV ldInst="LD0" cbName="svcb1">
          <s:Address><s:P type="MAC-Address">01-0C-CD-04-00-01</s:P><s:P type="APPID">4001</s:P></s:Address>
        </s:SMV>
      </s:ConnectedAP>
    </s:SubNetwork>
  </s:Communication>
  <s:IED name="IED_TEST" desc="合成测试设备" manufacturer="Synthetic">
    <s:AccessPoint name="AP1"><s:Server>
      <s:Authentication/>
      <s:LDevice inst="LD0" ldName="EXPLICIT_LD">
        <s:LN0 lnClass="LLN0" inst="" lnType="T_LLN0">
          <s:DataSet name="dsEvents" desc="有序成员，含 DO 引用及重复成员">
            <s:FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" fc="ST"/>
            <s:FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
            <s:FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ind1" daName="stVal" fc="ST"/>
          </s:DataSet>
          <s:ReportControl name="brcbEvents" datSet="dsEvents" rptID="TEST_EVENTS" confRev="1" buffered="true" indexed="true" bufTime="20" intgPd="1000">
            <s:TrgOps dchg="true" qchg="true" period="true"/>
            <s:OptFields seqNum="true" timeStamp="true" dataSet="true" reasonCode="true"/>
            <s:RptEnabled max="2"/>
          </s:ReportControl>
          <s:ReportControl name="urcbEvents" datSet="dsEvents" confRev="1" buffered="false" indexed="false">
            <s:TrgOps dchg="true"/><s:OptFields seqNum="true"/><s:RptEnabled max="1"/>
          </s:ReportControl>
          <s:GSEControl name="gcb1" type="GOOSE" appID="TEST_GOOSE" datSet="dsEvents" confRev="1"/>
          <s:SampledValueControl name="svcb1" smvID="TEST_SV" datSet="dsEvents" confRev="1" smpRate="80" nofASDU="1" multicast="true"><s:SmvOpts refreshTime="true" sampleSynchronized="true" sampleRate="true"/></s:SampledValueControl>
        </s:LN0>
        <s:LN lnClass="GGIO" inst="1" lnType="T_GGIO" desc="合成遥信">
          <s:DOI name="Ind1"><s:DAI name="stVal"><s:Val>true</s:Val></s:DAI></s:DOI>
        </s:LN>
      </s:LDevice>
    </s:Server></s:AccessPoint>
  </s:IED>
  <!-- Templates deliberately follow their users (forward references). -->
  <s:DataTypeTemplates>
    <s:LNodeType id="T_LLN0" lnClass="LLN0"><s:DO name="Beh" type="T_ENS"/></s:LNodeType>
    <s:LNodeType id="T_GGIO" lnClass="GGIO"><s:DO name="Ind1" type="T_SPS"/></s:LNodeType>
    <s:DOType id="T_ENS" cdc="ENS"><s:DA name="stVal" bType="Enum" type="T_MODE" fc="ST" dchg="true"/></s:DOType>
    <s:DOType id="T_SPS" cdc="SPS"><s:DA name="stVal" bType="BOOLEAN" fc="ST" dchg="true"/><s:DA name="q" bType="Quality" fc="ST" qchg="true"/><s:DA name="t" bType="Timestamp" fc="ST"/></s:DOType>
    <s:EnumType id="T_MODE"><s:EnumVal ord="1">on</s:EnumVal><s:EnumVal ord="2">blocked</s:EnumVal></s:EnumType>
  </s:DataTypeTemplates>
</s:SCL>
