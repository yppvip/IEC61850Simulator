<?xml version="1.0" encoding="UTF-8"?>
<!-- Intentional semantic/lexical failures and an unknown edition; not XSD validated. -->
<SCL xmlns="http://www.iec.ch/61850/2003/SCL">
  <Header id="Task6MmsLimitedSynthetic" version="2007B4-is-not-a-root-version"/>
  <IED name="Demo"><AccessPoint name="AP"><Server><LDevice inst="LD0"><LN0 lnClass="LLN0" inst="" lnType="Zero">
    <DataSet name="Duplicate"/><DataSet name="Duplicate"/>
    <ReportControl name="invalid" datSet="Absent" buffered="yes" indexed="perhaps" rptID="" confRev="1">
      <TrgOps dchg="sometimes"/><OptFields/><RptEnabled max="0"><ClientLN iedName="External" apRef="Unknown" ldInst="L" lnClass="GGIO" lnInst="1"/></RptEnabled>
    </ReportControl>
    <ReportControl name="associationSpecific" buffered="false" indexed="false" confRev="1">
      <TrgOps/><OptFields/><RptEnabled max="2"/>
    </ReportControl>
    <ReportControl name="ambiguous" datSet="Duplicate" buffered="true" indexed="true" confRev="1">
      <TrgOps/><OptFields/><RptEnabled max="1"/>
    </ReportControl>
  </LN0></LDevice></Server></AccessPoint></IED>
  <DataTypeTemplates><LNodeType id="Zero" lnClass="LLN0"/></DataTypeTemplates>
</SCL>
