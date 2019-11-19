#include "nanobench.h"

#include <array>
#include <string>
#include <string_view>

using namespace std::literals;

template <typename ST>
static bool cmpx(const std::string& a, ST b)
{
    return a == b;
}

void bench_string_cmp(ankerl::nanobench::Config& cfg)
{
    const std::array diff_strs = {
            "UWPa7RXjTSmFXVg0C5Fz vDOJZTGZuiHW1qTUrWyA CHVKIAE35cAN3Za42Zig eDr5n22vBMNRRdEFhdHB"s,
            "MVJ4PHN4qnkEJYRMjZsJ Nhd2km7Fd3rzfAf2UyOn ObrXrxbX5zFKRYwSInD0 AzkalJaZHGzf9G6FFKi9"s,
            "BHWxTFmuh5kz4AkhzPWU Cu0PLD7N5PHAgS85OQnA 1IlyeMc5lSwdrr4ICZeC NuqltzpIefM6nhvnSoRG"s,
            "zQOvwLsseHKXrvc6JF5y ZhUAtzuweGfPzELWQR9M q6FpR88T3ZRcvNiheliS MIo8btz2dtBSq8Gjy3bE"s,
            "MEMcqkm6X67sp7OotqVT N306gfECsaC0PpCbsD3D avHqrdrudnPhEK6YDFa3 4F1WMS2faqmwonFrjbi6"s,
            "cFfEU3mG3WRElIiGfORA QOYY7JaJQlckQ3pFcZ0l VevVQQBnjsHNhGR9t4Jh 1UicgEXmoJWXtzqfg84Y"s,
            "DVbSFAKBOrW44BTGqWpH 25lVfjvkgG3BwKHBMose Bqn9AfVlJzYF8Xg371ks mQ3ktmaiUi2dprVlUwMN"s,
            "W4aLv7yznq0k3ssc0PQu OkfZ9V6PkjzntD1kcnTS FlLwGvDZmr4fnCM7dqiR N0fDyQBysqa3BljbMFei"s,
            "MvhJvPsXZbzp5Zmupl4o F2M56A5Ukq9j2y49K2VP P3gKHgVXWFO4M771m1Of VVJl7U2kxrP14mEkEviT"s,
            "BsdE07SkEVS0c5TIonxW 5jhVRn8bFAZSodtCbA7f pb58MXdy3Rxg5LIAVoPO vIkEqdPWvb11MKP5YqYC"s,
            "JVE4zDiPPxYDYzzZp5Ac w4VZEKe9JJB11fHrocCZ QNpKxPF2VMIZfK65Uc4A dnZzUGSE4o7rXqhwATpo"s,
            "gj2rRqn4Oa9DvSNO05ca rCVE8qiUsWyGlzTcj5td tuBSj0uUiVCkvBSmJC5n F34pdyc12rltenKpVEfY"s,
            "Dtr6GPqKaL5uFi0n5SAH nK1axzHddvOeoiFK622j KjDebQesiJ49FyVWduQF Oc46DO2cfNEEdw2exBlE"s,
            "vkd6d6MlyoFOCOL2LIpL I1pMhFdsJY9UZjXwov6I OD0drRRPKLwMykwzIftO CmxPs8l1LI99SccJ5Zay"s,
            "JUYK14Wt4AXqIpifvaVC SD6p3VEPdLxlTgxEmu3f HcxwpxvLFacjvCOh9DAT 7GJM9Ud1qJuJIVbIxZSI"s,
            "l2NtHLivAwpKisCklkYx uAzoW0lbQgCm96My6bQE xfdY2JVmnfrqkC6iBOfZ TzQbgrmEIFiCZt7CWWCV"s,
            "Umc8Wb17OCsI2KBIbTlb N3dSj86y91gUAFlcq7pJ LHxKOk3Z6IYNSRIra0uD PrugeGPEgoreRqeQ0gDG"s,
            "rBwlxtGOIT1fXis1vViS 05zrTX3LubswNlDMCoS9 7n3OdDIjwXCa9rxLD4YQ QRssT2R3tgKDNI7TTi0b"s,
            "qr5EKoZzTiXn01dBmYkL IxT5a7I06kHy8lNqXRAT 2BlkNJSb9Aqx0MWXR4vF PRbDWPKNB3TskYnl3bOk"s,
            "KkkjRnzLRl1apDx4AyKS lpAI446HsQNvCh42QgU9 dtbEzTTp9bOJDEHyJraK 5xOrqOQGt5oK6UTsCQWK"s,
            "wHQ8Oje57SuLL1xixVrD 7kQwVgMN584v0k6AVVhd KqZNIHCUqvx6fHyzTVsk isrYCo7erYhOCSY4DRtj"s,
            "6IiWiU3vQz0MjLGblJRF M9sUF5qyjC6WgegLe5ck LNr3oGepnX1zAsS95C8n BAvZl6WBEbD4sir1Iz4a"s,
            "vmr7Y7UVB31IdWQGZry6 dVoPn8F8kLRHpi6bC4eF MDYYGGSAZGpMMTwLWByr 7Aas4lFo0bJESijDNQNi"s,
            "kMOAffQphnqCb2OrYTdW oHpQKB7F9ouYoxtu7pcV cH7Dg5Sgj0sk5uAaf8QN 4mAubeMwEeQoJiOHciSG"s,
            "JH9SfKGUF2CNcRl4lULj m3zlL7iVDr7YCRtTOkLK Os6EJyC7itGJktNHERK0 9ybhhT7ZoCuJ3wurFr4V"s,
    };

    const std::array same_strs = {
            "bSUxH3xZHj5sFo1eQuH3 ECJEKtqqyU5LxJC0Db4K slZkskDfzlJ0vFOZrlja McA7wxcAWCzJ105uK6gq"s,
            "vrYVPPUijiZfjmL3beqe gSBt7uuOj32Byk3KShR9 iXGniOr2MBcrObCB99Um VWOBXlz59f59lvkEgKAE"s,
            "NmIS09BTFRZPfJQhBlAw OP8WfBCSFscW1nMC8Zw8 tirtmNPBm0utUgTXxcN1 HspCTGxJWEgRytdqmIwS"s,
            "6hdhzrKENXlZczq5JRI6 2uvEhjNjD6nV1d5dtOEw umDErc7huBDcvJZu4vhU BMwVj1WCe0nfTPhyygWJ"s,
            "qFuK6Scj1ptlDoB7fwIJ L5JGCksswWl5FYX0ongn X7D1ouyibs91sxYpVPkq YhTlMJByrC1zRWmjPbdS"s,
            "mKFDxy6ETZIBBENxGton 9juQ1n1u8WCoaIzQPqQK TP4oeIhIoHjD2KxoQHq7 rBRihvk4RViyutEizqQV"s,
            "9iApgjcFow4wpWuNOubQ AYoApZQGuPeQsjrKrT04 E1YcEFLV3bi3UnRKJDlN pjxGrnSlG2afsXnq9uvC"s,
            "G012uIUxE5ncikxOGHBF 9f1UZSEMOpWRkSWnU09x 9d7Njj89WhC7yRRlmapu zbhS9TO0xDDPFDBRPhV7"s,
            "UgbdtDjPlBN5q9Jvkjyn TQffsVfi9PvI4et2oQnR wyzagoNlNxf6juCqNgr1 u0MHopaw8E7PFIRmiEtg"s,
            "PMe85f2RRf5hrDUde89Q InVwPFe1DYRUfpwHAOXO HH6tZQMiMUj6FN2f8xKy Ikqb1LNQQ30SwCeqkS3A"s,
            "qHSN2FZTMJf0Np2seiZl 3vT4ZJlsMramfYutFWTU OFX0zZQWtUuVVWSGkHnf U0dTMxaQvb82nEZyHm7W"s,
            "Xm4EYi9y94CnLAOf53rv 9WssdFD6FikesGPpWHIR 6DEaYYLFZEMdzWX3iHJw tZzUnCO2BwtHDXYJVvJn"s,
            "V8eIyOaqM3olzyOk7kHu g3y59hVKMusLNFBGhVah eUkJ6RAAYCwSjVDlgusV 8apukkg8l9WqKWAlsJY0"s,
            "fruqnruib5sghR6AagRW 8h9xHOLPGuaeZY87fkFx mJJrFRSxRHYGWaTnXpAi eDYtURTGc7czdcmR400Y"s,
            "5QhY2QJ4vGQrcwZt3tas Ah1s2BwY9ByWUP4pyAVh yDUikN1qfGc4DdZRju9W 3LowFTDTIJUV5l89pYDv"s,
            "ko8GhH9eKlZJgEHP4V9q 8fTqE4AyLr191T3mQou7 jEsvjoVT2t0JHLZROsoK CuZntJKAFGTyTW8SGXId"s,
            "Q5ZOdLXLS4X1Zzk4PP7j 0JDiRdd9bsarfKZXWD4w QeKodNpKjdwemK5l0VH3 6oyu5ZRlfB5Hy6CBsLAx"s,
            "MD3A3dIZS3iWKQCpoXbv 8cQMpGiDJEPMYBfkbeWt 8hUhJoQYZBhqsZ66Khlx wiKobLCBcpUbf5zh20Hf"s,
            "OYzhbJqdVfscK1OizW2C 3K0rAaYa98eciE37OpFN bgAn5uHPPsJY8acHknED 4TEX0xsoBXP4Gj6y3ApA"s,
            "N935ZHjhiPJPzi31Nm9E R4ZoVOuS97ogNIQdxj4G xVHo3Mpk9uR5S7SduDzH efUtWGE29giKOh9Pl39R"s,
            "pxgx1nWMsVJC6lyAi9Um Ymm6v2UWEPTB6aRskRkQ MA4HDdEQuqBQHrqzetsh DOBZWlfKHnBF3pyMUq4G"s,
            "aGonyUwH5QWmCBP4T0XE q7DlBfJ98WOQuM5WzA1Z OKEzpacUrgIGmaoZmAaF 4EMcMkgVc2EvZQgM7MC5"s,
            "orZ7k1w89IDZiwqzBRBE ky6hbTHHFrWAYsWwyvN8 VNTPRJom85WC2WfsXlPt eB1WpwtLs2PVcjKUAIaV"s,
            "wrbRBXRxWZiy3bqw2ml9 12eM1Pcb1RsP1fgCyNJ7 Y5ZCEXxIHVxLKvZMg5J6 1uNyYHeiZftnCRI58TtU"s,
            "tWCwRbOdKQwAi8yu7hds oRhtVvwEGCioCygcQcSg ek9fUtq5j2YY27a9BGbd C4bIXYSO1SvELQlTVzoe"s,
    };

    constexpr std::array same_sviews = {
            "LfmvE4Z0wpUaEnYsmnnV cwaE0kaH99InmbJQ44Z2 XYaX1FU4ZMYNKY441CQk u30WvHYRQUnGDLn4xnPI"sv,
            "JfSQDf1Lyddxxv0r7IbR 8syFIxCc6t9Yuow1VGk2 b3oXNfuSFuxlqzyz6hJX X8okXY3Jpitpix4ij5uI"sv,
            "5tezwPidg0Fu77OuSQZs TfFbOBmcrNKnqoClG31p 5zsH5PCFVnwl90B9y2mV aNp4LlSNOYs3z3zfmbrC"sv,
            "nGkBipnkv0XIbpMTaaOh RT50rLX8BZaFhfUFPVJk 9E8MKdcUgTw01uwqm94Z B4qKs5oQYVOyCGieTUzM"sv,
            "iaLNqxDqgZFQFHvOMMq8 IiPfrOyAqhQQ1Yk3FAk2 agnvyL0NjyzhzYvknfJI 5sbxvKIaDHofqXlWwQEV"sv,
            "Po6L9gE1dToYlmYxTqer hXYCLWcOPnGoCYCbmfiF gdVnqswPJ6AfY6lD6syd hoQCYk3Zh0LtPhBqu61l"sv,
            "VZdBeBkaqpJI0iU7Om7D BustzkvfKN4KlXmWtAr5 GxPOvtAvBlup6LZWBZr8 Sz3ieGMsssioC8DQvZZI"sv,
            "yc9zvisypgEzquYmocOk 0T2EkHFeJVWwiQKfDnEk RBL31Yv5pOfDQ4wWbw5U rqntzaxwDOXwLRcPP31c"sv,
            "6ImVPfzwCmvx3iygSx8b xAuP0CT2dwcc5KGAMNHp FdtxCBZ91E4AvJIu4Tmh Jyn3rk7nY19CwnRazE1T"sv,
            "tKubPNLg46jSnmZdzCwc f5HjxcIsFDlLsyMlh70f wJ31oSjWPQgwRaTKkqB2 CMe1iD6sod9yvXEplUWY"sv,
            "ghoeAjQ5VdiuB8q1SJGO yZqIS8lmeVLTM46dqEFZ aEWqM9q9GTUaCp0K1gaG 1xlbUoQJyyCqlcsY9jkt"sv,
            "gomeUUAmSPT3v77hmq53 Glu8gdeOZZutX4Mqgyz1 Clwxn8tagtv1rZnLUf9E mpaAOjH5SwEVXnR5rt6a"sv,
            "oMqxXasjJXH0I6wDN7DA 6NZ2qdbIC2zCF1u864lk pX0FTYNwlwIfs8Ttk4mO o1MIsp44G356pH3XnNKn"sv,
            "4D4IYbxAUcHUWsw7FHUk miUAupybvKSDZMfj1gOr vYFFdZYLvOx83mJJfg7m bbb6V3UlsAi6eJpaUg7M"sv,
            "RgTiy8mezhBETgibvyNA G2pLCU4bQsCv03cDbvl7 GvF7v2bbJyosNwq5aGKr 4WiWJcbw7W9sUJsIsHlC"sv,
            "IxgtECSktiTYwWCYy77L x4hKiybpysccMGnbFDaE ePLkGq6Fc6MbeHJtAgry 7O7Rrv7rYoBnIElwz22V"sv,
            "2xd7hTfwDsChKCMnmQT1 mweXWMs13VhqpSlxaqQv okAZWdHs6NW4Wrx0qIMo o1GZhwpV1DEkPWfk5zve"sv,
            "Cxpk0fGDHTLa52E0ZTHU VqB1bPw1ZAkHvutyY68u XcU2qPEyDj1Qo5UXgffT yYlPN2wsb5AkpwhrTBdS"sv,
            "OLm81qUx7eTfKQHUKu3D 8souAtLNZ3LX696ja0pI MCGkm5mU6LMItcpO7oy0 BcELp4MFEQYH0DcWq2uA"sv,
            "gSuvQrvNA7JkD2jiFiD2 8xSk9cGS2x9lxJjbU8Ar RGYCPDiKzGuP0yIXTwGc 1sWTvfVbkyzXQfY7t3uf"sv,
            "pC7vax09PzzhYLoboC4r psbJv89XVccrXca0nqN6 3sgG8O8Uep7YxfQXK5OF 0Bhlm4edmPZ9sn7s8QL3"sv,
            "utDjlAwnimF93ZFdEmJ0 3GfDGM1djjDwEQe3tTji 3t6aiESIAT3h4JabzCHo drakYOmeqSX3glpfPVfX"sv,
            "C6cEmTckgDjxOahexDcm SkQiEXNEsjMaI5uWlRBG 6aqj8WzR2lHowSq1Zk50 V8C3uf1GIi6LpKuPT9Vg"sv,
            "Hs3AtSRVKiFvCOeB3QnT CerffQ7kdhPt6ImF93Ee xHElGsx8Cgvpb2MQ20Jb jxilJpa4DuUkqShccSwf"sv,
            "oFWz9wpXN5JsCH0AetYb K8XlAbT8bfbe6kLG4sck TJuuXJlwJCdgR64wa4Ud mhLAhHejyXd50xiqPF5T"sv,
    };

    constexpr std::array same_cstrs = {
            "oVStdNxTqrdE0zH3OIjr LzLzv8XodxUZXRBCNOzG QOYBEBiVSh59ZDy6KiAm LtJP5A4yTsp413ANQk3y",
            "pQqZaVMeRFYzPAV8ApLn fSlF4zjKrQOHKGOV3edc r2DUzPCyFEON5i4Qlht2 Aq2jsZgAp1DTS71YRYlN",
            "wiJ2jKZeBY0dnWIC0fQ9 MGkrgKD9XO0WHOlXaqrh ezp4V7mOZynTtNJ9caRB Ha53iRn3GqcY3E05N7ee",
            "SWKETkUQuTJIF5s4T9gi 3OTmtNXQ39x5UHzGeM0J G1Vu9eV8ts8VVSjhxTso IUScsbNmR95yKhYYTbp8",
            "vQsByHG2mkMGwM2RIfoB HdGDDMQBEffwDKnu3PHD bItobGq7VC9SlKTLmWo7 SPHW2idYpkYWaZQZYf4O",
            "uh5khSxfoBmHNM8l1hkY Bw06Gsf92l9nVrsJ38jG Ai6rXbVGe1rn22mKHAW0 ApkdLpXdk6bn0bhnFmIZ",
            "gWQ0ur6iwEvQe9gBjAob onVvZCHZyTPdUyKGW34z vDfhmppTN0oBYiQNPg8h yXt9L7CX4i7xr67p552M",
            "OTWFkvYOkDq7IuI9x2b5 KxrnYgIeljcjkauSwPoh 8ih1pzvggjshM0UgwZUo mpp7gHssfQweEbgL7ArR",
            "lvcoaHGzElm4akGafpWI abOMRz183UEKKNxh63wL 4QEXRKlNrFs4i9U6RyBa ZXfdbtmZ2fcnFW5nCwqi",
            "pfOBQVLwmOw2ROdqdFnO 9xpviDVVRa1Lhd7N47Eb X0JOM7at7VQhIoyx3M0A Pdqu8kOCJh0t8JPOZU9M",
            "k39jg0PFmy1DeiwYUWD1 rIPYN87UOXI9VIc9x5Ry mh5G94qQcjTcq9H5dpSG Ak4JZq9cHzWTUc3ugquy",
            "Rt3dZqy1t1cBx77mAgHO YTM2zwJE4jO0P4kqqbym l69e3VefRxdGXBYkg70B gf1bxaTgf7zDPAFzq0r2",
            "wvaOwXqZGViL5R6rJPgh RHYp1PoPp8VDJjRniFmr JDQuZVjnx7akJKPeEtrg Dt9D2RYgo0ZPSon0Kj1N",
            "ey5vPpJUILnDV5pjeInn ZLztuNL4w6BeoWl0ypZD sPttztQBbhNbpe1H3a2f nGMo0297RReu43gVy0zp",
            "XB9nyZQFibWc6iVM7nsD umbN8sHAWQll7tXh1S1M rYYCa9vTMAyRHqmTSGts RP7NNG4j2QN9B7oBZOkT",
            "dpuLJUHIBwQJkSsIevk5 ozS5pvlr0j8nX1JLmFHP yUteZl7hhaRANy5jaaOk 32PEpGi1KTHc2P4g8cIg",
            "I51dlfBtwDR539hcNa7F UiaFrgXqIFBYkRiLxOUw 7ayieNNXYrJWJVSdOcGM xq1htpkjcV8BVUsctnex",
            "SfZVsbTLedMP2EN9tT1Q wChVwSxShbau6fNAHUQ2 255Ukev46EeJ5sRiw3PE 8MKd3R7cQRKLDNT3ohVk",
            "yMc6UjG55iuPqsY52Aad 2yr4UC91fAn4vomjp3vW DKXiR7hjw0XCksMgOtEf 3uniquOZ9YDX7v618LOq",
            "BbXc44T20jZt3oUQdT7V x6toxKlb8BC1gOP2i9NT k2m8vgdLxsZPam5xRtMw CbaVWeEauafGhNTjIJSX",
            "3j8yUbgQ4J2esApmcXGF q2STlqJx78JJJq2vkBIU DhzvlFWYBRJ1oQTTnf6Q IsEY7EqvosZKAqzstujF",
            "MJERxtOzNvs3CLcWxMi2 PiCgCEThcYm4fGTFOeX7 cwOE9ECj4sKjI5Te1mau aln7rf7xmslAnzciOlCj",
            "moDQlUFpnNdQBvoX35Ld JXbysmAleBJ3lEl6AOZl fbRPt3F1UQETUld7og2u lZwfnlYixBoQpeyKAne2",
            "MBpwtjMe0NhKduT3fZjU F6jaJDnTGQZG8KZwZLVO EY3L8mCFzmk0q49MthNF ZTLpc8iXFbgpQT8kzcrU",
            "lBJWwMlaHMPeD1dMTG60 4LwCok7wnxx9nOUUlEYW DBrBiJmPR6SN8I7Tnjbl 2BudmXioVBaQgC2j6z3k",
    };

    bool val = false;
    cfg.run("cmp std::string to std::string same", [&] {
        for (std::size_t i = 0; i < same_strs.size(); i++) {
            val ^= cmpx<const std::string&>(same_strs[i], same_strs[i]);
        }
    }).doNotOptimizeAway(&val);

    cfg.run("cmp std::string to std::string diff", [&] {
        for (std::size_t i = 0; i < same_strs.size(); i++) {
            val ^= cmpx<const std::string&>(diff_strs[i], same_strs[i]);
        }
    }).doNotOptimizeAway(&val);

    cfg.run("cmp std::string to std::string_view same", [&] {
        for (std::size_t i = 0; i < same_strs.size(); i++) {
            val ^= cmpx(same_strs[i], same_sviews[i]);
        }
    }).doNotOptimizeAway(&val);

    cfg.run("cmp std::string to std::string_view diff", [&] {
        for (std::size_t i = 0; i < same_strs.size(); i++) {
            val ^= cmpx(diff_strs[i], same_sviews[i]);
        }
    }).doNotOptimizeAway(&val);

    cfg.run("cmp std::string to c string same", [&] {
        for (std::size_t i = 0; i < same_strs.size(); i++) {
            val ^= cmpx(same_strs[i], same_cstrs[i]);
        }
    }).doNotOptimizeAway(&val);

    cfg.run("cmp std::string to c string diff", [&] {
        for (std::size_t i = 0; i < same_strs.size(); i++) {
            val ^= cmpx(diff_strs[i], same_cstrs[i]);
        }
    }).doNotOptimizeAway(&val);
}


