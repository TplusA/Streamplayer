<?xml version="1.0" encoding="UTF-8"?>

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

    <xsl:output indent="yes" encoding="UTF-8"/>

    <xsl:key name="testsuites-by-name" match="/report/result/test-case" use="name"/>

    <xsl:template match="/">
        <testsuites>
            <xsl:for-each select="/report/result/test-case[count(. | key('testsuites-by-name', name)[1]) = 1]">
                <testsuite>
                    <xsl:attribute name="name">
                        <xsl:value-of select="name"/>
                    </xsl:attribute>

                    <xsl:attribute name="time">
                        <xsl:value-of select="sum(/report/result/test-case[name = current()/name]/../elapsed)"/>
                    </xsl:attribute>

                    <xsl:attribute name="tests">
                        <xsl:value-of select="count(/report/result/test-case[name = current()/name])"/>
                    </xsl:attribute>

                    <xsl:apply-templates select="/report/result/test-case[name = current()/name]/.."/>
                </testsuite>
            </xsl:for-each>
        </testsuites>
    </xsl:template>

    <xsl:template match="result">
        <testcase>
            <xsl:attribute name="name">
                <xsl:value-of select="test/name"/>
            </xsl:attribute>

            <xsl:attribute name="classname">
                <xsl:value-of select="test-case/name"/>
            </xsl:attribute>

            <xsl:attribute name="time">
                <xsl:value-of select="elapsed"/>
            </xsl:attribute>

            <xsl:if test="status != 'success'">
                <failure>
                    <xsl:attribute name="type">
                        <xsl:value-of select="status"/>
                    </xsl:attribute>

                    <xsl:value-of select="backtrace/entry/file"/><xsl:text>:</xsl:text><xsl:value-of select="backtrace/entry/line"/>
                    <xsl:text>&#xa;</xsl:text>
                    <xsl:value-of select="backtrace/entry/info"/>
                    <xsl:text>&#xa;</xsl:text>
                    <xsl:text>&#xa;</xsl:text>
                    <xsl:text>Error: </xsl:text><xsl:value-of select="detail"/>
                </failure>
            </xsl:if>
        </testcase>
    </xsl:template>

</xsl:stylesheet>
