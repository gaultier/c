#include <stdio.h>

static const char input[] =
    "qzbzwwghwhwdhdqhhbhfbfsstggdsssgzgdzzdbbbzmbzzvlldppjnjlltwtsszwswgssjnsjn"
    "nfqfzqqjzjfjmfmwfmfhfnnmdnmnllbzzlbzlzflldzdbzbsbdddpgdppzhphhjjtccjgcgrgj"
    "gcjgjjsbjbffsgsqqrsqqgppgmgcmmrdrqdqbqmmctchcgcdgdwdwcdcjcfjccmpmlpllqqpmp"
    "llhfhchppwwdmdhdphpvprrhwhgwwlrwlwggwlwqqnmqnqrrbbzdzwddqzznllzwlljsscqqtl"
    "ltppqspswsttlrttnvvvwllfslsjsfjssnqnbbbvgvlltjtltjljmljjfgfbggcbgbmbjjvwjv"
    "jgvgzvvzddrjdddrwrlrlgldlhlwwcddbsbbtwwcssrnndvvsbsnbsbmbnmngnpgpphfhzhshl"
    "lltqtppnrrzpzrpzzrttmbmssbrbmbsmsnspnsncscwwmjwmwdmwmrmvmqvqvjqvvnrrtntwww"
    "wsrwrnwnrwwwlslrlhrllvpvhppdpprrgzrgzggqmqgmgvghhsmhhjljfflrflfrlrzzbjjqmm"
    "mcncddvhhzjjqzzjfzzbssjqqtsqtqccmttdhttmwtmtffspplhlrhrdhhhpghgrhrchhmgmqg"
    "qlglssvnnwrrrrqqbmmpgmpmdmmljlflzzjttqntqqcnqccrvcczffclffmvfvwvmvnvsvnvff"
    "czzljzztdtztcztctbtppmrprqprpbbhlhphphhvppvdvmddvcctvvjbvvpmpbpnpllvclvlql"
    "wqqpcpffdwdsdttzftfddbrrgsrgsgvgpgjpggfvgfvvtqtnnscssmnnfqnnblnnjlnlbbjrrm"
    "mpgmgnnqbbcjbcjbbmjjljsjrsjrsrwrgwgpgqgccqppfdpdssffgdgwddcvdvzzpttjlttmww"
    "hnnlntncttfvfwvvwrvrwwlhlwlvwvzvpvfpvvsbslblrlssjddwhwfhwffqjfjzfjfmfrrvsr"
    "rlbrrdfdmdwmddpnpfnpfnfppczpprzprpvpjjtltptvvrgvvsttflfvfqqhnnbmnbmmpsptpd"
    "pttrbbhjbbdnntppbsbmssgccfdfflppbcpbblclrlhrhttffvqqvvnpnmpnnrbnnhqnqrrwvw"
    "rrbvbffcvctcjcwwmzmvvtdvtvbtblbvlbvlvdvdrdsrdrprllnzznzhhcjjsgglfglgrlrwwf"
    "cfzfjzfztfztzggffjftjffcbfbhhwfwnnbssrpsppmllszstztwwgbwgbgtbgggztgztzwtzt"
    "bbrffznndhdmmqggssjfssrgssbnsndnqnvqvqvjqvqttlctltvlttrzrqrrdprddpqqvppvdd"
    "gmdddpldllnvvjdjcjdjvvznzqqbgqbgbpbnbvvdjvvsnndsdjjjhnhggrnnrvrvlvqllqhhsb"
    "hssvvlhlssgqgmqqcscpsspsrpsrpspvvdmvdvwwcjwwvhwvvmzvzvfvccfzczjccscbcrcjrr"
    "rsgrsspnsntsnnrnwwhdwwzhzbbwgwcgglvlttqrttvrrgprrljlddnttqrqffgdfgdgzzhght"
    "hhmdmsmtsshhsfhhgzgvzgzwzffvlffzjjrttsdddbldljlvjvjttwdwzzrbblwbbgmbmgmbgg"
    "bdbsbqqwzzgnzgzczpzfztftbtwbtbsttrwrfrssbrrdqdjqdqppdsswrwttmgmllqbqnnpspj"
    "jzjjmbbsddgjgbjggqqlgqgmgdmmwcmwcmwwsddvgvpphmhqmmlvlpvlpltllphhcshhrsrgsg"
    "zssspjspplgghvvwhwgwssbhshvvscshhbccbtcbcvvmnndsdwwbzbffhmmbppfjppbsblbtll"
    "tggzczdczdzjdjpjtppwdwlwqwnwjnnvttmcmzzqccvfcvvmrmzmwmhmttgdgmsrlddnbgqfbb"
    "mrpdglpbtdqpmctzjrffvvqnltcqlfddwqhvjpzhczzwhsmjtrpgsdtqlcqsljsjzqwhcfwttg"
    "ghsjqhzqlgjzgrtgttwblpprbppcpzsbntrwwmvfvbmjjrjwjqcjhnstmvwzrbnjpzznnctrtl"
    "lzhtwttntjqwjnfspzccqpjlzbncgbjjjztbgfmzflzsqflflcrfhtlsgbdfdbtwwqfdrqhzmm"
    "qdqthwzwqqqzddfvbwhnqwqtgwhtzlqqpwhcjhglcmmncvfcmqdwnzzmjbflwjrcjzwcblftzr"
    "pdcjzcmtzccjbsqmcnfmbsmrvlhswnmrqdczvvzzcnffgljdbtlvjgrqttcjhjmcdllnvhcdpz"
    "tqghfgvjcqmqvmdwhcgrtwpjlmjfbqmnjnbvvjczcwfcrhcgzmsvmjlplcpghnqtpzddnwtmrw"
    "mqwbttsnlngszfbnslqvlbzbfzqnjpdvcdpmjpmmjhvzwwgzfjfqwbqwrznhsdpjgsvzlsrtlm"
    "hjrfnwrmljlqzwnfnsmqtzfsldwrgmbrrvcmmmdmwflwnvpwsrrstmffwbtwqmjzdnzbwmqfrf"
    "dgsmhzhprdsgsqtljqhtdqmvnzlwhrrqqftbvnhmjnzvmbdqpjhzjnszgcfptfcmthpfcvfvdm"
    "wdswzfgwjblglfbpfvvwrmnzlcvtgqbcrhfqlffrznnhbtbwdwdldthbhdsqvnghpqdlvpfmzh"
    "jbtdhfmrdzpghtppmvddnphcnnczvznwzrvtcwvpslhrhmhzbzqtjqjvdpwncjbqdnwwpnfblq"
    "tqdwwqncbvtsncfhgncrprvhvzppwjfpdmtdrmtfcqdmdhwzrhpnrvspfbzhsvlpwzgrrhnrll"
    "hmpcdfcqdhcnphlffpbfmbsznmvfdgshbsjcjjvhqqfpmjgpdpcrglbqdrnqmftqtnwcsgqzdw"
    "ntfplsnlhdbdmcgwfldzzgmzjsdnldbnlnhjqhpmnsljsbhrglhjbbrmlhrmjnvzhnlvnfpdzf"
    "zwwdpzwhnqhlbqhrgfmvzpctdvscqbgznzdsvvvvcjnmbzlvsptslmqnggqjcmgvtgbnrzlmzq"
    "zmtdfvhjqqcpvbngmngjvrtcfvsmbvthvhmdcfhthmnjcvsvmsbwsjtmrwshspcsmnhvzqvgln"
    "tngbmgtdngbvvcjrznpdmmsssljnnqjwwfzgwtlfqqpsqghjgrghldrcdmcqvqdjsfrfrnlnvl"
    "tpfjhmcclwbsdcbmvmlnwltztfggzmrtnsczwvqgpqtwffmphprgrmjlnftlgqjvrngbcndlmr"
    "blmvcjsdhdcmtgsztjttpbgcznfcgdwfwdnqmrrjgdgrgshjfjqrbsjdhblvrqhlfphnbdslrj"
    "szcnpfhhwrfhhdvqbjpsmzznzbtbsgcggtctfbsrscvzlplfhlwjlrmzhbwjqhffdhrwcdctwv"
    "ttwqzbdtrhdhdvtgvdbzjtqvdgbpwtzqqqnljztgpbjjcrgdgmrrsfvngcdbgzvcfpdppbgfrm"
    "mdnqdvtlwwglmghjwfjjrwjfnwgwdplbmlfljhsmshwvvvtdnvfcbbwplwnvzfcjwgsrrlctps"
    "rhprttcjgcmfsjggfvbbljsbjtzplvjdwnwgsgvvntjwdwdqbwmtnnlgdcmfcccnwnrjlqtznj"
    "hdzwfpbzhjdmwwpcmffcwsnpfhmgqgwwnvpmqvrdfhlqtghrrbggdmtvpqgqsspmchnrqnrmql"
    "ddnspcdrwqpvclhrvjtzhpvthpltwqqbrdfjtnwncwrmdqszdpmmdzmjwjvqnfszvbchfdvwzh"
    "trfgfhfmgwprdpqgmbfntsqztvqmtjvgbsjvzsbhfznbbzrstqbrrmqdjcztmfpnwbmvtccmvl"
    "htvmgfdzcchbccrzznscbdwrdtnpslvcqmgrrvwhnjgjdpvbfgsdtdcmhpnwfwnnntqjqnwzfw"
    "nhsrjbhtqtlncvsnhgvtntfwldqfzztcdctsscfdmtnmdqgqgwmtqhlmswtqrvqbchdwtjsdlq"
    "jvfjdtmzlvrzwvfprzvjzrrfn";

int main() {
  const char *s = input;
  while (*s != 0) {
    const char a = s[0];
    const char b = s[1];
    const char c = s[2];
    const char d = s[3];

    if (a == b || a == c | a == d | b == c | b == d | c == d) {
      s += 4;
      continue;
    }

    printf("%ld %c %c %c %c", s - input + 3, a, b, c, d);
    break;
  }
}
