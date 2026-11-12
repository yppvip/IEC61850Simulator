<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL">
  <Header id="SyntheticTask4"/>
  <IED name="Source">
    <AccessPoint name="A"><Server><LDevice inst="L">
      <LN0 lnClass="LLN0" inst="" lnType="Zero">
        <DataSet name="Events">
          <FCDA ldInst="L" lnClass="XCBR" lnInst="1" doName="Pos" fc="ST"/>
          <FCDA ldInst="L" lnClass="XCBR" lnInst="1" doName="Pos" daName="stVal" fc="ST"/>
        </DataSet>
        <GSEControl name="EventsGo" type="GOOSE" appID="EventsText" datSet="Events" confRev="1"/>
      </LN0>
      <LN lnClass="XCBR" inst="1" lnType="Breaker"/>
      <LN lnClass="XCBR" inst="2" lnType="Breaker"/>
      <LN lnClass="MMXU" inst="1" lnType="Meter"/>
      <LN lnClass="GGIO" inst="1" lnType="Settings"/>
    </LDevice></Server></AccessPoint>
    <AccessPoint name="B"><Server><LDevice inst="L">
      <LN0 lnClass="LLN0" inst="" lnType="Zero"/>
      <LN lnClass="XCBR" inst="1" lnType="Breaker"/>
    </LDevice></Server></AccessPoint>
  </IED>
  <IED name="Sink"><AccessPoint name="A"><Server><LDevice inst="L">
    <LN0 lnClass="LLN0" inst="" lnType="Zero"><Inputs>
      <ExtRef iedName="Source" ldInst="L" lnClass="XCBR" lnInst="2" doName="Pos" daName="stVal"/>
      <ExtRef iedName="Outside" ldInst="L" lnClass="XCBR" lnInst="1" doName="Pos" daName="stVal"/>
    </Inputs></LN0>
  </LDevice></Server></AccessPoint></IED>
  <DataTypeTemplates>
    <LNodeType id="Zero" lnClass="LLN0"><DO name="Beh" type="State"/></LNodeType>
    <LNodeType id="Breaker" lnClass="XCBR"><DO name="Pos" type="Position" desc="模板位置"/></LNodeType>
    <LNodeType id="Meter" lnClass="MMXU"><DO name="A" type="Phases"/></LNodeType>
    <LNodeType id="Settings" lnClass="GGIO"><DO name="Limit" type="Setting"/></LNodeType>
    <DOType id="State" cdc="SPS"><DA name="stVal" bType="BOOLEAN" fc="ST"/></DOType>
    <DOType id="Position" cdc="DPC">
      <DA name="stVal" bType="Dbpos" fc="ST"/>
      <DA name="q" bType="Quality" fc="ST"/>
      <DA name="t" bType="Timestamp" fc="ST"/>
      <DA name="Oper" bType="Struct" type="Operate" fc="CO"/>
      <DA name="ctlModel" bType="Enum" type="ControlModel" fc="CF"/>
    </DOType>
    <DOType id="Phases" cdc="WYE"><SDO name="phsA" type="Measured"/></DOType>
    <DOType id="Measured" cdc="CMV"><DA name="cVal" bType="Struct" type="Complex" fc="MX"/></DOType>
    <DOType id="Setting" cdc="ASG"><DA name="setMag" bType="Struct" type="Magnitude" fc="SP"/></DOType>
    <DAType id="Operate"><BDA name="ctlVal" bType="BOOLEAN"/></DAType>
    <DAType id="Complex"><BDA name="mag" bType="Struct" type="Magnitude"/></DAType>
    <DAType id="Magnitude"><BDA name="f" bType="FLOAT32"/></DAType>
    <EnumType id="ControlModel"><EnumVal ord="0">status-only</EnumVal></EnumType>
  </DataTypeTemplates>
</SCL>
