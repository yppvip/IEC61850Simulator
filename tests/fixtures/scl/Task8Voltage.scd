<?xml version="1.0" encoding="UTF-8"?>
<!-- Synthetic offline behavior fixture. No engineering or runtime data. -->
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B" release="4">
  <Header id="Task8VoltageSynthetic"/>
  <IED name="DemoListed">
    <AccessPoint name="AP1"><Server>
      <LDevice inst="LD0" ldName="ListedMeasurements">
        <LN0 lnClass="LLN0" inst="" lnType="Zero">
          <DataSet name="Voltage">
            <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.mag.f" fc="MX"/>
            <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" fc="MX"/>
          </DataSet>
          <ReportControl name="VoltageReport" datSet="Voltage" confRev="1" buffered="true" indexed="true">
            <TrgOps dchg="true"/><OptFields seqNum="true"/><RptEnabled max="1"/>
          </ReportControl>
        </LN0>
        <LN lnClass="MMXU" inst="1" lnType="FullMeasurements" desc="合成电压测量回路">
          <DOI name="PhV"><SDI name="phsA"><SDI name="cVal">
            <SDI name="mag"><DAI name="f"><Val>110</Val></DAI></SDI>
            <SDI name="ang"><DAI name="f"><Val>15</Val></DAI></SDI>
          </SDI></SDI></DOI>
          <DOI name="AngSet"><SDI name="setMag"><DAI name="f"><Val>30</Val></DAI></SDI></DOI>
        </LN>
        <LN lnClass="PTOV" inst="1" lnType="Protection" desc="合成保护逻辑，无测量输入声明"/>
      </LDevice>
    </Server></AccessPoint>
  </IED>
  <IED name="DemoUnlisted">
    <AccessPoint name="AP1"><Server><LDevice inst="LD0">
      <LN0 lnClass="LLN0" inst="" lnType="Zero"/>
      <LN lnClass="MMXU" inst="1" lnType="FullMeasurements" desc="同名测量路径，未配置 FCDA"/>
    </LDevice></Server></AccessPoint>
  </IED>
  <IED name="DemoNoAngle">
    <AccessPoint name="AP1"><Server><LDevice inst="LD0">
      <LN0 lnClass="LLN0" inst="" lnType="Zero">
        <DataSet name="MissingAngle">
          <FCDA ldInst="LD0" lnClass="MMXU" lnInst="1" doName="PhV.phsA" daName="cVal.ang.f" fc="MX"/>
        </DataSet>
        <ReportControl name="AngleReport" datSet="MissingAngle" confRev="1" buffered="true" indexed="true">
          <TrgOps dchg="true"/><OptFields seqNum="true"/><RptEnabled max="1"/>
        </ReportControl>
      </LN0>
      <LN lnClass="MMXU" inst="1" lnType="MagnitudeMeasurements"/>
    </LDevice></Server></AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="Zero" lnClass="LLN0"/>
    <LNodeType id="Protection" lnClass="PTOV"/>
    <LNodeType id="FullMeasurements" lnClass="MMXU">
      <DO name="PhV" type="FullWye" desc="相对地电压"/>
      <DO name="AngSet" type="IndependentSetting" desc="合成相位定值"/>
    </LNodeType>
    <LNodeType id="MagnitudeMeasurements" lnClass="MMXU">
      <DO name="PhV" type="MagnitudeWye" desc="仅有幅值"/>
    </LNodeType>
    <DOType id="FullWye" cdc="WYE"><SDO name="phsA" type="FullPhase" desc="A 相"/></DOType>
    <DOType id="MagnitudeWye" cdc="WYE"><SDO name="phsA" type="MagnitudePhase" desc="A 相"/></DOType>
    <DOType id="FullPhase" cdc="CMV">
      <DA name="cVal" fc="MX" bType="Struct" type="Phasor"/>
      <DA name="q" fc="MX" bType="Quality"/>
      <DA name="t" fc="MX" bType="Timestamp"/>
    </DOType>
    <DOType id="MagnitudePhase" cdc="CMV"><DA name="cVal" fc="MX" bType="Struct" type="MagnitudeVector"/></DOType>
    <DOType id="IndependentSetting" cdc="ASG"><DA name="setMag" fc="SP" bType="Struct" type="AnalogFloat"/></DOType>
    <DAType id="Phasor">
      <BDA name="mag" bType="Struct" type="AnalogFloat"/>
      <BDA name="ang" bType="Struct" type="AnalogFloat"/>
    </DAType>
    <DAType id="MagnitudeVector"><BDA name="mag" bType="Struct" type="AnalogFloat"/></DAType>
    <DAType id="AnalogFloat"><BDA name="f" bType="FLOAT32"/></DAType>
  </DataTypeTemplates>
</SCL>
